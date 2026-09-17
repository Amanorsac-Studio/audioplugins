using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using Microsoft.Win32;

namespace AmanorsacInstaller;

internal static class Program
{
    // Written by installer/build-windows-installer.ps1 so one bootstrapper can
    // package any Amanorsac product or bundle.
    private const string ProductName = BuildInfo.ProductName;
    private const string Version = BuildInfo.Version;

    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        var quiet = args.Contains("--quiet", StringComparer.OrdinalIgnoreCase);
        var noShortcuts = args.Contains("--no-shortcuts", StringComparer.OrdinalIgnoreCase);
        var noRegistry = args.Contains("--no-registry", StringComparer.OrdinalIgnoreCase);
        var installRootOverride = ArgumentValue(args, "--install-root");
        var vst3RootOverride = ArgumentValue(args, "--vst3-root");
        if (!quiet)
        {
            var answer = MessageBox.Show(
                $"Install {BuildInfo.ProductName} ({BuildInfo.PluginCount} plug-ins) for the current Windows user?"
                    + Environment.NewLine + Environment.NewLine
                    + "VST3 and Standalone versions install for this user only. No administrator rights are needed.",
                ProductName,
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Question);
            if (answer != DialogResult.Yes) return;
        }

        try
        {
            Install(installRootOverride, vst3RootOverride, noShortcuts, noRegistry);
            if (!quiet)
                MessageBox.Show(
                    "Installation complete.\n\nRestart your DAW and run a VST3 rescan.\n\nPlugins were installed to the standard per-user VST3 folder.",
                    ProductName,
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
        }
        catch (Exception exception)
        {
            if (quiet) File.WriteAllText(Path.Combine(Path.GetTempPath(), "AmanorsacInstallerError.txt"), exception.ToString());
            else MessageBox.Show(exception.ToString(), ProductName + " — Installation failed",
                                 MessageBoxButtons.OK, MessageBoxIcon.Error);
            Environment.ExitCode = 1;
        }
    }

    private static string? ArgumentValue(string[] args, string name)
    {
        for (var index = 0; index + 1 < args.Length; ++index)
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase)) return args[index + 1];
        return null;
    }

    private static void Install(string? installRootOverride, string? vst3RootOverride,
                                bool noShortcuts, bool noRegistry)
    {
        var localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        var appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        var vst3Root = Path.GetFullPath(vst3RootOverride ?? Path.Combine(localAppData, "Programs", "Common", "VST3", "Amanorsac Studio"));
        var installRoot = Path.GetFullPath(installRootOverride ?? Path.Combine(localAppData, "Programs", ProductName));
        var temporaryRoot = Path.Combine(Path.GetTempPath(), "AmanorsacInstall_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temporaryRoot);

        try
        {
            var archivePath = Path.Combine(temporaryRoot, "payload.zip");
            ExtractResource("payload.zip", archivePath);
            ZipFile.ExtractToDirectory(archivePath, temporaryRoot, true);

            var vst3Source = Path.Combine(temporaryRoot, "VST3");
            var standaloneSource = Path.Combine(temporaryRoot, "Standalone");
            var bundles = Directory.GetDirectories(vst3Source, "*.vst3", SearchOption.TopDirectoryOnly);
            var apps = Directory.GetFiles(standaloneSource, "*.exe", SearchOption.TopDirectoryOnly);
            // The build records how many plug-ins it packaged; anything else is a
            // damaged or partial download.
            if (bundles.Length != BuildInfo.PluginCount || apps.Length != BuildInfo.PluginCount)
                throw new InvalidDataException($"Payload validation failed: {bundles.Length} VST3 bundles and {apps.Length} Standalone apps.");

            Directory.CreateDirectory(vst3Root);
            foreach (var bundle in bundles)
            {
                var destination = Path.Combine(vst3Root, Path.GetFileName(bundle));
                if (Directory.Exists(destination)) Directory.Delete(destination, true);
                CopyDirectory(bundle, destination);
            }

            var standaloneRoot = Path.Combine(installRoot, "Standalone");
            Directory.CreateDirectory(standaloneRoot);
            foreach (var app in apps) File.Copy(app, Path.Combine(standaloneRoot, Path.GetFileName(app)), true);
            ExtractResource("uninstall.ps1", Path.Combine(installRoot, "uninstall.ps1"));
            ExtractResource("README.md", Path.Combine(installRoot, "README.md"));

            if (!noShortcuts)
            {
                var startMenu = Path.Combine(appData, "Microsoft", "Windows", "Start Menu", "Programs", "Amanorsac Studio");
                Directory.CreateDirectory(startMenu);
                foreach (var app in Directory.GetFiles(standaloneRoot, "*.exe")) CreateShortcut(app, startMenu);
            }

            if (!noRegistry)
            {
                var uninstallCommand = $"powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File \"{Path.Combine(installRoot, "uninstall.ps1")}\" -InstallRoot \"{installRoot}\" -Vst3Root \"{vst3Root}\"";
                using var key = Registry.CurrentUser.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\AmanorsacStudioMixingSuite", true)
                                ?? throw new InvalidOperationException("Could not create uninstall registration.");
                key.SetValue("DisplayName", ProductName);
                key.SetValue("DisplayVersion", Version);
                key.SetValue("Publisher", "Amanorsac Studio");
                key.SetValue("InstallLocation", installRoot);
                key.SetValue("UninstallString", uninstallCommand);
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
            }
        }
        finally
        {
            if (Directory.Exists(temporaryRoot)) Directory.Delete(temporaryRoot, true);
        }
    }

    private static void ExtractResource(string name, string destination)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        using var source = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)
                           ?? throw new InvalidDataException("Missing embedded installer resource: " + name);
        using var target = File.Create(destination);
        source.CopyTo(target);
    }

    private static void CopyDirectory(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (var file in Directory.GetFiles(source)) File.Copy(file, Path.Combine(destination, Path.GetFileName(file)), true);
        foreach (var directory in Directory.GetDirectories(source)) CopyDirectory(directory, Path.Combine(destination, Path.GetFileName(directory)));
    }

    private static void CreateShortcut(string executable, string startMenu)
    {
        var shellType = Type.GetTypeFromProgID("WScript.Shell");
        if (shellType is null) return;
        dynamic shell = Activator.CreateInstance(shellType)!;
        dynamic shortcut = shell.CreateShortcut(Path.Combine(startMenu, Path.GetFileNameWithoutExtension(executable) + ".lnk"));
        shortcut.TargetPath = executable;
        shortcut.WorkingDirectory = Path.GetDirectoryName(executable);
        shortcut.Save();
    }
}
