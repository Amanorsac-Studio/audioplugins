using System.IO.Compression;
using System.Reflection;
using System.Text.Json;
using Microsoft.Win32;

namespace AmanorsacInstaller;

/// <summary>What the buyer chose on the Components page.</summary>
internal sealed class InstallPlan
{
    public bool InstallVst3 { get; set; } = true;
    public bool InstallApps { get; set; } = true;
    /// <summary>Standard VST3 folder. Fixed for buyers; only tests override it.</summary>
    public string Vst3Root { get; set; } = InstallEngine.StandardVst3Root;
    /// <summary>The product's own folder: applications, README and the uninstaller.</summary>
    public string AppRoot { get; set; } = InstallEngine.DefaultAppRoot;
    /// <summary>Optional second plug-in folder. Empty means none.</summary>
    public string ExtraVst3Root { get; set; } = "";
    public bool Shortcuts { get; set; } = true;
    public bool Register { get; set; } = true;
    public string ProgressFile { get; set; } = "";
    public string ResultFile { get; set; } = "";
}

/// <summary>One line of the final page: what went where.</summary>
internal sealed record InstalledItem(string Component, string Path);

/// <summary>
/// Everything that touches the disk. It has no window, so the same code serves
/// the wizard, the elevated worker and the silent mode the tests drive.
/// </summary>
internal static class InstallEngine
{
    public static string StandardVst3Root =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonProgramFiles), "VST3");

    public static string DefaultAppRoot =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Amanorsac Studio", BuildInfo.ProductName);

    public static string UninstallKeyName =>
        "Amanorsac_" + new string(BuildInfo.ProductName.Where(char.IsLetterOrDigit).ToArray());

    public static List<InstalledItem> Apply(InstallPlan plan, Action<int, string> progress)
    {
        if (!plan.InstallVst3 && !plan.InstallApps)
            throw new InvalidOperationException("Nothing was selected to install.");

        var installed = new List<InstalledItem>();
        var manifest = new List<string> { "product|" + BuildInfo.ProductName, "version|" + BuildInfo.Version };
        var temporaryRoot = Path.Combine(Path.GetTempPath(), "AmanorsacInstall_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temporaryRoot);

        try
        {
            progress(2, "Unpacking");
            var archivePath = Path.Combine(temporaryRoot, "payload.zip");
            ExtractResource("payload.zip", archivePath);
            ZipFile.ExtractToDirectory(archivePath, temporaryRoot, true);
            File.Delete(archivePath);

            var bundles = Directory.GetDirectories(Path.Combine(temporaryRoot, "VST3"), "*.vst3", SearchOption.TopDirectoryOnly);
            var apps = Directory.GetFiles(Path.Combine(temporaryRoot, "Standalone"), "*.exe", SearchOption.TopDirectoryOnly);
            // The build records how many plug-ins it packaged; anything else is a
            // damaged or partial download.
            if (bundles.Length != BuildInfo.PluginCount || apps.Length != BuildInfo.PluginCount)
                throw new InvalidDataException($"This download is incomplete: {bundles.Length} plug-ins and {apps.Length} applications of {BuildInfo.PluginCount}. Please download it again.");

            var steps = (plan.InstallVst3 ? bundles.Length * (string.IsNullOrWhiteSpace(plan.ExtraVst3Root) ? 1 : 2) : 0)
                      + (plan.InstallApps ? apps.Length : 0) + 2;
            var done = 0;
            void Step(string what) => progress(10 + (int)(85.0 * ++done / steps), what);

            if (plan.InstallVst3)
            {
                foreach (var root in new[] { plan.Vst3Root, plan.ExtraVst3Root }.Where(r => !string.IsNullOrWhiteSpace(r)))
                {
                    Directory.CreateDirectory(root);
                    foreach (var bundle in bundles)
                    {
                        var destination = Path.Combine(root, Path.GetFileName(bundle));
                        if (Directory.Exists(destination)) Directory.Delete(destination, true);
                        CopyDirectory(bundle, destination);
                        manifest.Add("path|" + destination);
                        installed.Add(new InstalledItem("VST3", destination));
                        Step(Path.GetFileNameWithoutExtension(bundle));
                    }
                }
            }

            // The product folder always exists: it is where the README and the
            // uninstaller live, whether or not the applications were chosen.
            Directory.CreateDirectory(plan.AppRoot);
            if (plan.InstallApps)
            {
                foreach (var app in apps)
                {
                    var destination = Path.Combine(plan.AppRoot, Path.GetFileName(app));
                    File.Copy(app, destination, true);
                    manifest.Add("path|" + destination);
                    installed.Add(new InstalledItem("Application", destination));
                    Step(Path.GetFileNameWithoutExtension(app));
                }
            }

            foreach (var support in new[] { "README.txt", "uninstall.ps1", "product.ico" })
                ExtractResource(support, Path.Combine(plan.AppRoot, support));
            installed.Add(new InstalledItem("Read me", Path.Combine(plan.AppRoot, "README.txt")));
            Step("Finishing");

            if (plan.Shortcuts && plan.InstallApps)
            {
                var startMenu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms), "Amanorsac Studio", BuildInfo.ProductName);
                Directory.CreateDirectory(startMenu);
                foreach (var app in Directory.GetFiles(plan.AppRoot, "*.exe")) CreateShortcut(app, startMenu);
                manifest.Add("path|" + startMenu);
            }

            File.WriteAllLines(Path.Combine(plan.AppRoot, "installed.txt"), manifest);

            if (plan.Register)
            {
                var uninstall = $"powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File \"{Path.Combine(plan.AppRoot, "uninstall.ps1")}\" -InstallRoot \"{plan.AppRoot}\"";
                using var key = Registry.LocalMachine.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\" + UninstallKeyName, true)
                                ?? throw new InvalidOperationException("Could not register the product with Windows.");
                key.SetValue("DisplayName", BuildInfo.ProductName);
                key.SetValue("DisplayVersion", BuildInfo.Version);
                key.SetValue("Publisher", "Amanorsac Studio");
                key.SetValue("DisplayIcon", Path.Combine(plan.AppRoot, "product.ico"));
                key.SetValue("InstallLocation", plan.AppRoot);
                key.SetValue("UninstallString", uninstall);
                key.SetValue("URLInfoAbout", "https://amanorsac.studio");
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
            }

            Step("Done");
            progress(100, "Done");
            return installed;
        }
        finally
        {
            try { if (Directory.Exists(temporaryRoot)) Directory.Delete(temporaryRoot, true); } catch { /* temp is best effort */ }
        }
    }

    /// <summary>The elevated worker: reads a plan, reports progress to a file the wizard polls.</summary>
    public static int RunWorker(string planPath)
    {
        var plan = JsonSerializer.Deserialize<InstallPlan>(File.ReadAllText(planPath))
                   ?? throw new InvalidDataException("Unreadable install plan.");
        try
        {
            var installed = Apply(plan, (percent, what) => WriteAtomically(plan.ProgressFile, percent + "|" + what));
            WriteAtomically(plan.ResultFile, "ok\n" + string.Join("\n", installed.Select(i => i.Component + "|" + i.Path)));
            return 0;
        }
        catch (Exception exception)
        {
            WriteAtomically(plan.ResultFile, "error\n" + exception.Message);
            return 1;
        }
    }

    private static void WriteAtomically(string path, string content)
    {
        if (string.IsNullOrEmpty(path)) return;
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, content);
        File.Move(temporary, path, true);
    }

    public static Stream? OpenResource(string name) => Assembly.GetExecutingAssembly().GetManifestResourceStream(name);

    private static void ExtractResource(string name, string destination)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        using var source = OpenResource(name) ?? throw new InvalidDataException("Missing installer resource: " + name);
        using var target = File.Create(destination);
        source.CopyTo(target);
    }

    private static void CopyDirectory(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (var file in Directory.GetFiles(source)) File.Copy(file, Path.Combine(destination, Path.GetFileName(file)), true);
        foreach (var directory in Directory.GetDirectories(source)) CopyDirectory(directory, Path.Combine(destination, Path.GetFileName(directory)));
    }

    private static void CreateShortcut(string executable, string folder)
    {
        var shellType = Type.GetTypeFromProgID("WScript.Shell");
        if (shellType is null) return;
        dynamic shell = Activator.CreateInstance(shellType)!;
        dynamic shortcut = shell.CreateShortcut(Path.Combine(folder, Path.GetFileNameWithoutExtension(executable) + ".lnk"));
        shortcut.TargetPath = executable;
        shortcut.WorkingDirectory = Path.GetDirectoryName(executable);
        shortcut.Save();
    }
}
