namespace AmanorsacInstaller;

internal static class Program
{
    /// <summary>
    /// Modes:
    ///   (none)                 the five-page wizard a buyer sees
    ///   --apply plan.json      the elevated worker the wizard starts; no window
    ///   --quiet [overrides]    silent install, for automated tests and deployment
    ///   --screenshots folder   writes a PNG of each wizard page, as delivery evidence
    /// </summary>
    [STAThread]
    private static int Main(string[] args)
    {
        var applyPlan = ArgumentValue(args, "--apply");
        if (applyPlan is not null) return InstallEngine.RunWorker(applyPlan);

        if (args.Contains("--quiet", StringComparer.OrdinalIgnoreCase)) return RunQuiet(args);

        ApplicationConfiguration.Initialize();
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);

        var shots = ArgumentValue(args, "--screenshots");
        if (shots is not null)
        {
            using var wizard = new Wizard();
            wizard.CapturePages(shots);
            return 0;
        }

        Application.Run(new Wizard());
        return 0;
    }

    private static int RunQuiet(string[] args)
    {
        var plan = new InstallPlan
        {
            InstallVst3 = !args.Contains("--no-vst3", StringComparer.OrdinalIgnoreCase),
            InstallApps = !args.Contains("--no-apps", StringComparer.OrdinalIgnoreCase),
            Shortcuts = !args.Contains("--no-shortcuts", StringComparer.OrdinalIgnoreCase),
            Register = !args.Contains("--no-registry", StringComparer.OrdinalIgnoreCase),
        };
        if (ArgumentValue(args, "--vst3-root") is { } vst3) plan.Vst3Root = Path.GetFullPath(vst3);
        if (ArgumentValue(args, "--install-root") is { } root) plan.AppRoot = Path.GetFullPath(root);
        if (ArgumentValue(args, "--extra-vst3") is { } extra) plan.ExtraVst3Root = Path.GetFullPath(extra);

        try
        {
            InstallEngine.Apply(plan, (_, _) => { });
            return 0;
        }
        catch (Exception exception)
        {
            File.WriteAllText(Path.Combine(Path.GetTempPath(), "AmanorsacInstallerError.txt"), exception.ToString());
            return 1;
        }
    }

    private static string? ArgumentValue(string[] args, string name)
    {
        for (var index = 0; index + 1 < args.Length; ++index)
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase)) return args[index + 1];
        return null;
    }
}
