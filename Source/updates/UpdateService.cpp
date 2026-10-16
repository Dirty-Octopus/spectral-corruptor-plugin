// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#include "UpdateService.h"
#include <stdexcept>
#include <mutex>
namespace scrr::updates {
namespace {
const juce::String releaseRoot = "https://github.com/Dirty-Octopus/spectral-corruptor-plugin/releases/download/";
juce::File scheduledRecord()
{
    return dataDirectory().getChildFile ("scheduled-" + juce::String (processId()) + "-" + processStartIdentity() + ".json");
}
std::array<int, 3> stableVersion (juce::String s)
{
    if (s.startsWithChar ('v') || s.startsWithChar ('V')) s = s.substring (1);
    const auto parts = juce::StringArray::fromTokens (s, ".", "");
    if (parts.size() != 3) return { -1, -1, -1 };
    std::array<int, 3> values {};
    for (int i = 0; i < 3; ++i)
    {
        if (parts[i].isEmpty() || parts[i].length() > 8 || ! parts[i].containsOnly ("0123456789")) return { -1, -1, -1 };
        values[(size_t) i] = parts[i].getIntValue();
    }
    return values;
}
[[maybe_unused]] juce::String shellQuote (const juce::String& s) { return "'" + s.replace ("'", "'\\''") + "'"; }
[[maybe_unused]] juce::String psQuote (const juce::String& s) { return "'" + s.replace ("'", "''") + "'"; }
std::unique_ptr<juce::InputStream> stream (const juce::String& url)
{
    int code = 0;
    auto s = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs (8000).withExtraHeaders ("User-Agent: SpectralCorruptor/" JucePlugin_VersionString "\r\nAccept: application/vnd.github+json\r\n")
        .withStatusCode (&code).withNumRedirectsToFollow (5));
    if (! s || code != 200) throw std::runtime_error ("Unable to reach GitHub. Try again later.");
    return s;
}
}
std::shared_ptr<UpdateService> UpdateService::shared()
{
    // Processors own the service, never a global destructor. Joining a worker
    // after JUCE's Objective-C classes have been disposed is unsafe on macOS.
    static std::mutex registryLock;
    static std::weak_ptr<UpdateService> registered;
    const std::lock_guard<std::mutex> guard (registryLock);
    if (auto service = registered.lock()) return service;
    auto service = std::make_shared<UpdateService>();
    registered = service;
    return service;
}
int UpdateService::compareVersions (juce::String a, juce::String b)
{
    auto x = stableVersion (a.trim()), y = stableVersion (b.trim()); if (x[0] < 0 || y[0] < 0) return 0;
    return x < y ? -1 : x > y ? 1 : 0;
}
bool UpdateService::trustedAsset (const juce::String& url)
{
    if (! url.startsWith (releaseRoot)) return false;
    const auto path = juce::StringArray::fromTokens (url.substring (releaseRoot.length()), "/", "");
    if (path.size() != 2 || stableVersion (path[0])[0] < 0) return false;
    return path[1] == "SpectralCorruptor-macOS.pkg" || path[1] == "SpectralCorruptor-Windows-Setup.exe" || path[1] == "SHA256SUMS.txt";
}
UpdateService::UpdateService() : Thread ("Spectral Corruptor Updates")
{
    if (! testing)
    {
        const bool restored = restoreScheduled();
        startThread();
        if (! restored) check (false);
    }
}
UpdateService::~UpdateService() { signalThreadShouldExit(); notify(); waitForThreadToExit (-1); }
UpdateService::Snapshot UpdateService::snapshot() const { const juce::ScopedLock guard (lock); return status; }
void UpdateService::setStatus (State state, juce::String message, float progress)
{ const juce::ScopedLock guard (lock); status.state = state; status.message = std::move (message); status.progress = progress; }
bool UpdateService::persistScheduled()
{
    auto* record = new juce::DynamicObject();
    record->setProperty ("job", scheduledDirectory.getFullPathName());
    record->setProperty ("installer", downloaded.getFullPathName());
    record->setProperty ("sha256", expectedHash);
    record->setProperty ("version", status.version);
    juce::TemporaryFile temporary (scheduledRecord());
    return temporary.getFile().replaceWithText (juce::JSON::toString (juce::var (record)))
        && temporary.overwriteTargetFileWithTemporary();
}
void UpdateService::clearScheduledRecord() { scheduledRecord().deleteFile(); }
bool UpdateService::restoreScheduled()
{
    const auto file = scheduledRecord();
    if (! file.existsAsFile() || file.getSize() > 16384) return false;
    const auto record = juce::JSON::parse (file.loadFileAsString());
    const juce::File job (record["job"].toString()), installer (record["installer"].toString());
    const auto hash = record["sha256"].toString();
    const auto name = installer.getFileName();
    if (! installer.isAChildOf (dataDirectory()) || job.getParentDirectory() != installer.getParentDirectory()
        || ! job.getFileName().startsWith ("job-") || ! job.isDirectory()
        || (name != "SpectralCorruptor-macOS.pkg" && name != "SpectralCorruptor-Windows-Setup.exe")
        || hash.length() != 64 || ! hash.containsOnly ("0123456789abcdef"))
    { clearScheduledRecord(); return false; }
    downloaded = installer; scheduledDirectory = job; expectedHash = hash;
    status.version = record["version"].toString();
    if (! installer.existsAsFile())
    { clearScheduledRecord(); setStatus (State::failed, "The scheduled installer is missing. Check for updates again."); }
    else if (job.getChildFile ("cancel").existsAsFile())
    { clearScheduledRecord(); setStatus (State::ready, "Update installation cancelled."); }
    else setStatus (State::scheduled);
    return true;
}
void UpdateService::check (bool force)
{
    const juce::ScopedLock guard (lock);
    if (operation || status.state == State::checking || status.state == State::downloading || status.state == State::scheduled) return;
    forceCheck = force; status.state = State::checking; status.message.clear(); operation = 1; notify();
}
void UpdateService::download()
{
    const juce::ScopedLock guard (lock);
   #if defined(SCR_SOURCE_EDITION) && SCR_SOURCE_EDITION
    status.message = "Source edition: rebuild the newer source. Official binary installation is disabled.";
    return;
   #else
    if (operation || status.state != State::available) return;
    status.state = State::downloading; status.message.clear(); operation = 2; notify();
   #endif
}
void UpdateService::run()
{
    // Drain networking autoreleases before Thread::threadEntryPoint marks its
    // handle closed, while the owning processor still keeps JUCE alive.
    while (! threadShouldExit())
    {
        JUCE_AUTORELEASEPOOL
        int task = 0; { const juce::ScopedLock guard (lock); task = operation; operation = 0; }
        try { if (task == 1) performCheck(); else if (task == 2) performDownload(); }
        catch (const std::exception& e) { setStatus (State::failed, e.what()); }
        {
            const juce::ScopedLock guard (lock);
            if (status.state == State::scheduled)
            {
                const auto result = scheduledDirectory.getChildFile ("result.txt");
                if (result.existsAsFile())
                {
                    const auto message = result.loadFileAsString().trim();
                    if (message.isNotEmpty()) clearScheduledRecord();
                    if (message == "Cancelled") setStatus (State::ready, "Update installation cancelled.");
                    else if (message == "Installed") setStatus (State::current, "Update installed. Reopen the plugin to use the new version.");
                    else if (message.isNotEmpty()) setStatus (State::failed, message);
                }
            }
        }
        if (! threadShouldExit()) wait (500);
    }
}
juce::String UpdateService::readText (const juce::String& url, int64_t limit)
{
    auto input = stream (url); juce::MemoryBlock bytes; char buffer[8192];
    while (! input->isExhausted() && ! threadShouldExit())
    {
        auto n = input->read (buffer, (int) sizeof (buffer)); if (n <= 0) break;
        bytes.append (buffer, (size_t) n); if ((int64_t) bytes.getSize() > limit) throw std::runtime_error ("Update response is too large.");
    }
    if (threadShouldExit()) throw std::runtime_error ("Update cancelled.");
    return bytes.toString();
}
void UpdateService::performCheck()
{
    const auto cache = dataDirectory().getChildFile ("latest-release.json");
    bool force; { const juce::ScopedLock guard (lock); force = forceCheck; }
    juce::String response;
    const auto age = (juce::Time::getCurrentTime() - cache.getLastModificationTime()).inMilliseconds();
    if (! force && cache.existsAsFile() && cache.getSize() < 1024 * 1024 && age >= 0 && age < 6 * 60 * 60 * 1000)
        response = cache.loadFileAsString();
    else
    {
        response = readText ("https://api.github.com/repos/Dirty-Octopus/spectral-corruptor-plugin/releases/latest", 1024 * 1024);
        cache.getParentDirectory().createDirectory(); juce::TemporaryFile temp (cache);
        if (temp.getFile().replaceWithText (response)) temp.overwriteTargetFileWithTemporary();
    }
    auto json = juce::JSON::parse (response);
    auto version = json["tag_name"].toString();
    if (stableVersion (version)[0] < 0 || (bool) json["draft"] || (bool) json["prerelease"]) throw std::runtime_error ("No stable release is available.");
    { const juce::ScopedLock guard (lock); status.version = version; }
    if (compareVersions (version, JucePlugin_VersionString) <= 0) { setStatus (State::current); return; }
   #if JUCE_MAC
    assetName = "SpectralCorruptor-macOS.pkg";
   #elif JUCE_WINDOWS
    assetName = "SpectralCorruptor-Windows-Setup.exe";
   #else
    throw std::runtime_error ("Automatic installation is available on macOS and Windows.");
   #endif
    installerUrl.clear(); checksumUrl.clear();
    const auto assetList = json["assets"];
    if (auto* assets = assetList.getArray()) for (auto& asset : *assets)
    {
        if (asset["name"].toString() == assetName) installerUrl = asset["browser_download_url"].toString();
        if (asset["name"].toString() == "SHA256SUMS.txt") checksumUrl = asset["browser_download_url"].toString();
    }
    if (! trustedAsset (installerUrl) || ! trustedAsset (checksumUrl)
        || installerUrl != releaseRoot + version + "/" + assetName || checksumUrl != releaseRoot + version + "/SHA256SUMS.txt")
        throw std::runtime_error ("The release is missing a verified installer.");
   #if defined(SCR_SOURCE_EDITION) && SCR_SOURCE_EDITION
    setStatus (State::available, "Source edition: rebuild the newer source. Official binary installation is disabled.");
   #else
    setStatus (State::available);
   #endif
}
void UpdateService::performDownload()
{
    if (! updateLock.enter (0)) throw std::runtime_error ("Another instance is already preparing an update.");
    struct Unlock { juce::InterProcessLock& l; ~Unlock() { l.exit(); } } unlock { updateLock };
    const auto sums = readText (checksumUrl, 64 * 1024); juce::String expected;
    for (auto line : juce::StringArray::fromLines (sums))
    {
        auto words = juce::StringArray::fromTokens (line.trim(), " \t", ""); words.removeEmptyStrings();
        if (words.size() == 2 && words[1].trimCharactersAtStart ("*") == assetName) expected = words[0].toLowerCase();
    }
    if (expected.length() != 64 || ! expected.containsOnly ("0123456789abcdef")) throw std::runtime_error ("Installer checksum was not found.");
    auto folder = dataDirectory().getChildFile (juce::Uuid().toString());
    if (folder.createDirectory().failed()) throw std::runtime_error ("Cannot create the update download folder.");
    auto target = folder.getChildFile (assetName); auto partial = folder.getChildFile (assetName + ".part");
    auto input = stream (installerUrl); auto output = partial.createOutputStream();
    if (! output) throw std::runtime_error ("Cannot write the installer.");
    const int64_t total = input->getTotalLength(); int64_t received = 0; char buffer[65536];
    while (! input->isExhausted() && ! threadShouldExit())
    {
        int n = input->read (buffer, (int) sizeof (buffer)); if (n <= 0) break;
        received += n;
        if (received > 512 * 1024 * 1024 || ! output->write (buffer, (size_t) n)) throw std::runtime_error ("Installer download could not be written.");
        setStatus (State::downloading, {}, total > 0 ? (float) received / (float) total : 0);
    }
    output->flush(); if (output->getStatus().failed()) throw std::runtime_error ("Installer download could not be written."); output.reset();
    if (threadShouldExit() || (total > 0 && received != total)) throw std::runtime_error ("Installer download was interrupted.");
    if (juce::SHA256 (partial).toHexString().toLowerCase() != expected) { partial.deleteFile(); throw std::runtime_error ("Installer checksum mismatch. Installation is blocked."); }
    if (! partial.moveFileTo (target)) throw std::runtime_error ("Cannot finalise the downloaded installer.");
    downloaded = target; expectedHash = expected; setStatus (State::ready);
}
bool UpdateService::cancelScheduled()
{
    const juce::ScopedLock guard (lock);
    if (status.state != State::scheduled) return false;
    if (! scheduledDirectory.getChildFile ("cancel").replaceWithText ("cancel")) return false;
    clearScheduledRecord();
    setStatus (State::ready, "Update installation cancelled."); return true;
}
juce::String UpdateService::installScript (int hostPid, const juce::File& hosts, const juce::File& cancel, const juce::File& installer, const juce::String& sha256)
{
    const auto originIdentity = hostPid == processId() ? processStartIdentity() : juce::String();
   #if JUCE_WINDOWS
    return "param([switch]$ElevatedInstall)\n$ErrorActionPreference='Stop'\n$cancel=" + psQuote (cancel.getFullPathName())
        + "\n$registry=" + psQuote (hosts.getFullPathName()) + "\n$installer=" + psQuote (installer.getFullPathName())
        + "\n$expected=" + psQuote (sha256) + "\n$origin=" + juce::String (hostPid)
        + "\n$originIdentity=" + psQuote (originIdentity) + R"SC(
$job=Split-Path $cancel
$PSDefaultParameterValues['Set-Content:Encoding']='UTF8'
$resultFile=Join-Path $job 'result.txt'
$seenFile=Join-Path $job 'seen-hosts.txt'
$mutex=$null; $acquired=$false
function Wait-AudioHosts {
  $seen=@{}; $seen[$origin]=$originIdentity
  if(Test-Path -LiteralPath $seenFile){
    Get-Content -LiteralPath $seenFile | ForEach-Object {
      if($_ -match '^(\d+)(?: (\d+))?$'){$seen[[int]$Matches[1]]=[string]$Matches[2]}
    }
  }
  $deadline=(Get-Date).AddHours(24)
  do {
    if(Test-Path -LiteralPath $cancel){'Cancelled' | Set-Content -LiteralPath $resultFile; exit 0}
    Get-ChildItem -LiteralPath $registry -Filter '*.pid' -ErrorAction SilentlyContinue | ForEach-Object {
      $entry=(Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue)
      if($entry -and $entry.Trim() -match '^(\d+)(?: (\d+))?$'){$seen[[int]$Matches[1]]=[string]$Matches[2]}
    }
    $running=$false
    foreach($idNumber in $seen.Keys){
      $hostProcess=Get-Process -Id $idNumber -ErrorAction SilentlyContinue
      if($hostProcess){
        try {
          if(-not $seen[$idNumber] -or $hostProcess.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -eq $seen[$idNumber]){$running=$true}
        } catch { $running=$true }
      }
    }
    @($seen.Keys | ForEach-Object {"$_ $($seen[$_])".TrimEnd()}) | Set-Content -LiteralPath $seenFile -Encoding UTF8
    if(-not $running){return}
    if((Get-Date) -ge $deadline){throw 'Timed out waiting for audio hosts; installation was not started.'}
    Start-Sleep -Seconds 2
  } while($true)
}
function Verify-Installer {
  if((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLower() -ne $expected){throw 'Installer checksum changed; installation blocked.'}
}
try {
  if(-not $ElevatedInstall){
    $mutex=New-Object System.Threading.Mutex($false,'Local\SpectralCorruptorInstallJob')
    try {$acquired=$mutex.WaitOne(0)} catch [System.Threading.AbandonedMutexException] {$acquired=$true}
    if(-not $acquired){throw 'Another update installation is already scheduled.'}
    $PID | Set-Content -LiteralPath (Join-Path $job 'started')
  }
  Wait-AudioHosts
  Verify-Installer
  if(Test-Path -LiteralPath $cancel){'Cancelled' | Set-Content -LiteralPath $resultFile; exit 0}
  if($ElevatedInstall){
    $installResult=Start-Process -FilePath $installer -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/NOCLOSEAPPLICATIONS' -Wait -PassThru
    if($installResult.ExitCode -ne 0){throw "Installer failed with exit code $($installResult.ExitCode)"}
    'Installed' | Set-Content -LiteralPath $resultFile
  } else {
    # Re-enter after UAC approval, then check hosts and integrity again. Reopening
    # a host while the permission dialog is showing must postpone installation.
    $arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "'+$PSCommandPath+'" -ElevatedInstall'
    $elevated=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    if($elevated.ExitCode -ne 0){throw "Update helper failed with exit code $($elevated.ExitCode)"}
  }
} catch {
  if(-not (Test-Path -LiteralPath $resultFile)){$_.ToString() | Set-Content -LiteralPath $resultFile}
  exit 1
} finally {
  if($acquired){$mutex.ReleaseMutex()}
  if($mutex){$mutex.Dispose()}
}
)SC";
   #else
    return "#!/bin/sh\nset -eu\ntrap '' HUP\nexport LC_ALL=C\ncancel=" + shellQuote (cancel.getFullPathName())
        + "\nregistry=" + shellQuote (hosts.getFullPathName()) + "\ninstaller=" + shellQuote (installer.getFullPathName())
        + "\nexpected=" + shellQuote (sha256) + "\norigin=" + shellQuote (juce::String (hostPid) + " " + originIdentity) + R"SC(
job=$(/usr/bin/dirname "$cancel")
result="$job/result.txt"
# BSD kernel locks release on crashes. Keeping the inode avoids unlink/relock
# races and a stale on-disk file never counts as an active installation.
if [ "${1:-}" != --locked ] && [ "${1:-}" != --install ]; then
  /bin/mkdir -p "$registry"
  if /usr/bin/lockf -k -s -t 0 "$registry/install-job.lock" /bin/sh "$0" --locked; then exit 0; else
    code=$?
    [ -f "$result" ] || printf '%s\n' 'Another update is scheduled, or the update helper could not start.' > "$result"
    exit "$code"
  fi
fi
trap 'code=$?; if [ "$code" -ne 0 ] && [ ! -f "$result" ]; then printf "Update failed (exit %s). See installer.log or elevation.log.\n" "$code" > "$result"; fi' EXIT
if [ "${1:-}" != --install ]; then printf '%s\n' "$$" > "$job/started"; fi
seen="$job/seen-hosts.txt"
[ -f "$seen" ] || printf '%s\n' "$origin" > "$seen"
remember_hosts() {
  for file in "$registry/"*.pid; do
    [ -f "$file" ] || continue
    entry=$(/bin/cat "$file" 2>/dev/null) || continue
    case "$entry" in ''|*[!0-9\ ]*) continue;; esac
    /usr/bin/grep -Fqx -- "$entry" "$seen" || printf '%s\n' "$entry" >> "$seen"
  done
}
wait_hosts() {
  count=0
  while [ "$count" -lt 43200 ]; do
    if [ -f "$cancel" ]; then printf '%s\n' Cancelled > "$result"; exit 0; fi
    remember_hosts
    running=0
    while read -r pid birth extra; do
      case "$pid" in ''|*[!0-9]*) continue;; esac
      if /bin/kill -0 "$pid" 2>/dev/null; then
        if [ -z "${birth:-}" ]; then running=1; continue; fi
        started=$(TZ=UTC /bin/ps -p "$pid" -o lstart= 2>/dev/null) || continue
        current=$(TZ=UTC /bin/date -j -f '%a %b %e %T %Y' "$started" '+%s' 2>/dev/null) || { running=1; continue; }
        [ "$current" != "$birth" ] || running=1
      fi
    done < "$seen"
    [ "$running" -ne 0 ] || return 0
    /bin/sleep 2
    count=$((count+1))
  done
  printf '%s\n' 'Timed out waiting for audio hosts; installation was not started.' > "$result"
  exit 3
}
verify_installer() {
  actual=$(/usr/bin/shasum -a 256 "$installer" | /usr/bin/cut -d ' ' -f 1)
  if [ "$actual" != "$expected" ]; then printf '%s\n' 'Installer checksum changed; installation blocked.' > "$result"; exit 1; fi
}
wait_hosts
verify_installer
if [ -f "$cancel" ]; then printf '%s\n' Cancelled > "$result"; exit 0; fi
if [ "${1:-}" = --install ]; then
  choices='/Library/Application Support/SpectralCorruptor/Installer/choices.plist'
  if [ -f "$choices" ]; then
    # Apple's CLI requires customLocation directories to exist. Only create
    # directories for selected components, and keep every path a quoted argument.
    i=0
    while attribute=$(/usr/libexec/PlistBuddy -c "Print :$i:choiceAttribute" "$choices" 2>/dev/null); do
      if [ "$attribute" = customLocation ]; then
        key=$(/usr/libexec/PlistBuddy -c "Print :$i:choiceIdentifier" "$choices")
        selected=0; j=0
        while candidate=$(/usr/libexec/PlistBuddy -c "Print :$j:choiceIdentifier" "$choices" 2>/dev/null); do
          kind=$(/usr/libexec/PlistBuddy -c "Print :$j:choiceAttribute" "$choices")
          if [ "$candidate" = "$key" ] && [ "$kind" = selected ]; then selected=$(/usr/libexec/PlistBuddy -c "Print :$j:attributeSetting" "$choices"); break; fi
          j=$((j+1))
        done
        if [ "$selected" = 1 ]; then
          folder=$(/usr/libexec/PlistBuddy -c "Print :$i:attributeSetting" "$choices")
          case "$folder" in "/"*) /bin/mkdir -p "$folder";; *) echo 'Invalid saved component directory' > "$result"; exit 1;; esac
        fi
      fi
      i=$((i+1))
    done
    /usr/sbin/installer -pkg "$installer" -target / -applyChoiceChangesXML "$choices" > "$job/installer.log" 2>&1
  else
    /usr/sbin/installer -pkg "$installer" -target / > "$job/installer.log" 2>&1
  fi
  printf '%s\n' Installed > "$result"
else
  # The privileged branch repeats host identity and checksum checks after the
  # system's password dialog. This never closes or kills an audio host.
  /usr/bin/osascript - "$0" > "$job/elevation.log" 2>&1 <<'APPLESCRIPT'
on run arguments
  do shell script "/bin/sh " & quoted form of (item 1 of arguments) & " --install" with administrator privileges
end run
APPLESCRIPT
fi
)SC";
   #endif
}

bool UpdateService::writeInstallScript (const juce::File& file, const juce::String& content)
{
   #if JUCE_WINDOWS
    // Windows PowerShell 5.1 otherwise reads UTF-8 without BOM as legacy ANSI.
    return file.replaceWithText (content, true, true, "\r\n");
   #else
    return file.replaceWithText (content, false, false, "\n");
   #endif
}

bool UpdateService::schedule (bool projectsSaved)
{
   #if defined(SCR_SOURCE_EDITION) && SCR_SOURCE_EDITION
    juce::ignoreUnused (projectsSaved);
    setStatus (State::failed, "Source edition: rebuild the newer source. Official binary installation is disabled.");
    return false;
   #else
    const juce::ScopedLock guard (lock);
    if (! projectsSaved || status.state != State::ready || ! downloaded.existsAsFile()) return false;
    if (processStartIdentity().isEmpty())
    { setStatus (State::failed, "Cannot verify the audio host process. Installation is blocked."); return false; }
    if (expectedHash.isEmpty() || juce::SHA256(downloaded).toHexString().toLowerCase() != expectedHash)
    { setStatus(State::failed,"Installer checksum changed. Download again."); return false; }
    scheduledDirectory = downloaded.getParentDirectory().getChildFile ("job-" + juce::Uuid().toString());
    if (scheduledDirectory.createDirectory().failed()) return false;
    auto cancel = scheduledDirectory.getChildFile("cancel");
   #if JUCE_WINDOWS
    auto script = scheduledDirectory.getChildFile("install.ps1");
   #else
    auto script = scheduledDirectory.getChildFile("install.sh");
   #endif
    if (! writeInstallScript (script, installScript(processId(),dataDirectory().getChildFile("hosts"),cancel,downloaded,expectedHash))) return false;
    juce::ChildProcess helper;
   #if JUCE_WINDOWS
    if (! helper.start(juce::StringArray{"powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-WindowStyle","Hidden","-File",script.getFullPathName()},0)) return false;
   #else
    if (! helper.start(juce::StringArray{"/bin/sh",script.getFullPathName()},0)) return false;
   #endif
    // The platform mutex/lock is acquired before the helper acknowledges start.
    // A launched process alone does not prove this instance owns an update job.
    const auto started = scheduledDirectory.getChildFile ("started");
    const auto result = scheduledDirectory.getChildFile ("result.txt");
    for (int attempt = 0; attempt < 500; ++attempt)
    {
        if (result.existsAsFile() || ! helper.isRunning())
        {
            setStatus (State::ready, result.existsAsFile() ? result.loadFileAsString().trim() : "The update helper could not start.");
            return false;
        }
        if (started.existsAsFile())
        {
            if (! persistScheduled())
            {
                cancel.replaceWithText ("cancel");
                setStatus (State::ready, "Cannot save the update job. Installation was cancelled.");
                return false;
            }
            setStatus (State::scheduled); return true;
        }
        juce::Thread::sleep (10);
    }
    cancel.replaceWithText ("cancel");
    setStatus (State::ready, "The update helper did not acknowledge startup. Try again.");
    return false;
   #endif
}
}
