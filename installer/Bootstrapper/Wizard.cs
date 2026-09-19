using System.ComponentModel;
using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace AmanorsacInstaller;

/// <summary>
/// The five pages every Amanorsac installer shows, in the same order:
/// Welcome, Licence, Components, Installing, Done.
/// </summary>
internal sealed class Wizard : Form
{
    // ---- brand
    private static readonly Color Ground = Color.FromArgb(0x0B, 0x0D, 0x10);
    private static readonly Color Rail = Color.FromArgb(0x0F, 0x12, 0x16);
    private static readonly Color Panel = Color.FromArgb(0x14, 0x18, 0x1F);
    private static readonly Color Line = Color.FromArgb(0x26, 0x2C, 0x36);
    private static readonly Color TextMain = Color.FromArgb(0xE9, 0xED, 0xF2);
    private static readonly Color TextDim = Color.FromArgb(0x8A, 0x93, 0xA0);
    private static readonly Color Accent = ColorTranslator.FromHtml(BuildInfo.Accent);

    private static readonly PrivateFontCollection Fonts = new();
    private static readonly string Face = LoadFace();

    private static readonly string[] StepNames = { "Welcome", "Licence", "Components", "Installing", "Done" };

    private readonly Panel[] pages = new Panel[5];
    private readonly Label title = new();
    private readonly PillButton back = new(false), next = new(true), cancel = new(false);
    private readonly Image? logo;
    private int current;

    // Components page
    private readonly AccentCheck vst3Box = new(), appsBox = new(), acceptBox = new();
    private readonly TextBox appRoot = new(), extraRoot = new();
    private readonly Label componentsNote = new();

    // Installing page
    private readonly AccentBar bar;
    private readonly Label status = new();
    private readonly System.Windows.Forms.Timer poll = new() { Interval = 120 };
    private InstallPlan? plan;
    private Process? worker;

    // Done page
    private readonly TextBox summary = new();
    private readonly Label doneHeadline = new(), whatNext = new();

    public Wizard()
    {
        Text = BuildInfo.ProductName + " Setup";
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(860, 560);
        BackColor = Ground;
        ForeColor = TextMain;
        Font = UiFont(10f);
        DoubleBuffered = true;
        Icon = LoadIcon();
        using (var stream = InstallEngine.OpenResource("logo.png"))
            logo = stream is null ? null : Image.FromStream(stream);
        bar = new AccentBar(Accent, Line);

        BuildChrome();
        pages[0] = BuildWelcome();
        pages[1] = BuildLicence();
        pages[2] = BuildComponents();
        pages[3] = BuildInstalling();
        pages[4] = BuildDone();
        foreach (var page in pages) { page.Bounds = new Rectangle(250, 96, 580, 380); page.BackColor = Ground; Controls.Add(page); }

        poll.Tick += (_, _) => PollWorker();
        Show(0);
    }

    // ------------------------------------------------------------------ fonts and icon
    private static string LoadFace()
    {
        // Inter ships inside the installer when the studio supplies the font
        // files; otherwise an installed copy is used, then Segoe UI.
        foreach (var name in typeof(Wizard).Assembly.GetManifestResourceNames().Where(n => n.EndsWith(".ttf", StringComparison.OrdinalIgnoreCase)))
        {
            using var stream = InstallEngine.OpenResource(name);
            if (stream is null) continue;
            var bytes = new byte[stream.Length];
            stream.ReadExactly(bytes);
            var handle = Marshal.AllocCoTaskMem(bytes.Length);
            Marshal.Copy(bytes, 0, handle, bytes.Length);
            Fonts.AddMemoryFont(handle, bytes.Length);
        }
        if (Fonts.Families.Length > 0) return Fonts.Families[0].Name;
        using var installed = new InstalledFontCollection();
        return installed.Families.Any(f => f.Name == "Inter") ? "Inter" : "Segoe UI";
    }

    private static Font UiFont(float size, FontStyle style = FontStyle.Regular)
    {
        var family = Fonts.Families.FirstOrDefault(f => f.Name == Face);
        return family is not null ? new Font(family, size, style) : new Font(Face, size, style);
    }

    private static Icon? LoadIcon()
    {
        using var stream = InstallEngine.OpenResource("product.ico");
        return stream is null ? null : new Icon(stream);
    }

    // ------------------------------------------------------------------ chrome
    private void BuildChrome()
    {
        title.SetBounds(250, 40, 520, 40);
        title.Font = UiFont(19f, FontStyle.Bold);
        title.ForeColor = TextMain;
        title.BackColor = Color.Transparent;
        Controls.Add(title);

        var close = new Label { Text = "✕", Font = UiFont(11f), ForeColor = TextDim, TextAlign = ContentAlignment.MiddleCenter, Cursor = Cursors.Hand };
        close.SetBounds(ClientSize.Width - 44, 8, 36, 30);
        close.MouseEnter += (_, _) => close.ForeColor = TextMain;
        close.MouseLeave += (_, _) => close.ForeColor = TextDim;
        close.Click += (_, _) => Close();
        Controls.Add(close);

        back.Text = "Back";
        next.Text = "Next";
        cancel.Text = "Cancel";
        cancel.SetBounds(250, 500, 110, 38);
        back.SetBounds(ClientSize.Width - 30 - 140 - 12 - 110, 500, 110, 38);
        next.SetBounds(ClientSize.Width - 30 - 140, 500, 140, 38);
        foreach (var button in new[] { back, next, cancel }) { button.Font = UiFont(10f, FontStyle.Bold); button.Accent = Accent; Controls.Add(button); }
        back.Click += (_, _) => Show(current - 1);
        cancel.Click += (_, _) => Close();
        next.Click += (_, _) => Advance();
    }

    // The window has no system frame, so it is dragged by its own surface.
    [DllImport("user32.dll")] private static extern bool ReleaseCapture();
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr handle, int message, IntPtr w, IntPtr l);

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button != MouseButtons.Left || e.Y > 90) return;
        ReleaseCapture();
        SendMessage(Handle, 0xA1, 2, 0);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;

        using (var rail = new SolidBrush(Rail)) g.FillRectangle(rail, 0, 0, 220, ClientSize.Height);
        using (var pen = new Pen(Line))
        {
            g.DrawLine(pen, 220, 0, 220, ClientSize.Height);
            g.DrawLine(pen, 250, 484, ClientSize.Width - 30, 484);
            g.DrawRectangle(pen, 0, 0, ClientSize.Width - 1, ClientSize.Height - 1);
        }

        if (logo is not null)
        {
            var width = 160f;
            var height = width * logo.Height / logo.Width;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.DrawImage(logo, 30f, 34f, width, height);
        }

        using var dim = new SolidBrush(TextDim);
        using var main = new SolidBrush(TextMain);
        using var accent = new SolidBrush(Accent);
        using var stepFont = UiFont(10.5f);
        using var stepBold = UiFont(10.5f, FontStyle.Bold);
        for (var i = 0; i < StepNames.Length; ++i)
        {
            var y = 170 + i * 44;
            var active = i == current;
            var passed = i < current;
            using var ring = new Pen(active || passed ? Accent : Line, 2f);
            if (passed) g.FillEllipse(accent, 32, y + 3, 14, 14);
            else g.DrawEllipse(ring, 33, y + 4, 12, 12);
            if (active) g.FillEllipse(accent, 36, y + 7, 6, 6);
            g.DrawString(StepNames[i], active ? stepBold : stepFont, active ? main : dim, 58, y);
            if (i < StepNames.Length - 1)
                using (var link = new Pen(passed ? Accent : Line, 2f)) g.DrawLine(link, 39, y + 20, 39, y + 44);
        }

        using var small = UiFont(8.5f);
        g.DrawString("Amanorsac Studio", small, dim, 30, ClientSize.Height - 52);
        g.DrawString("Version " + BuildInfo.Version, small, dim, 30, ClientSize.Height - 34);
    }

    // ------------------------------------------------------------------ pages
    private Label Caption(Panel page, string text, int x, int y, int width, int height, float size = 10f, bool dim = false, FontStyle style = FontStyle.Regular)
    {
        var label = new Label { Text = text, Font = UiFont(size, style), ForeColor = dim ? TextDim : TextMain, BackColor = Color.Transparent };
        label.SetBounds(x, y, width, height);
        page.Controls.Add(label);
        return label;
    }

    private LinkLabel Link(Panel page, string text, string url, int x, int y, int width)
    {
        var link = new LinkLabel { Text = text, Font = UiFont(10f), LinkColor = Accent, ActiveLinkColor = TextMain, VisitedLinkColor = Accent, BackColor = Color.Transparent, LinkBehavior = LinkBehavior.HoverUnderline };
        link.SetBounds(x, y, width, 22);
        link.LinkClicked += (_, _) => Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        page.Controls.Add(link);
        return link;
    }

    private void Style(AccentCheck box, string text)
    {
        box.Text = text;
        box.Font = UiFont(10.5f, FontStyle.Bold);
        box.ForeColor = TextMain;
        box.Accent = Accent;
    }

    private void Style(TextBox box)
    {
        box.Font = UiFont(9.5f);
        box.BackColor = Panel;
        box.ForeColor = TextMain;
        box.BorderStyle = BorderStyle.FixedSingle;
    }

    private Panel BuildWelcome()
    {
        var page = new Panel();
        Caption(page, BuildInfo.ProductName, 0, 6, 580, 44, 22f, false, FontStyle.Bold);
        Caption(page, "Version " + BuildInfo.Version + "   ·   Amanorsac Studio", 2, 56, 580, 24, 10.5f, true);
        Caption(page, BuildInfo.OneLine, 2, 104, 560, 60, 11.5f);
        Caption(page, $"This installs {BuildInfo.PluginCount} plug-ins. You will choose which parts to install and see exactly where each one goes before anything is written.", 2, 176, 560, 60, 10f, true);
        Caption(page, "Nothing else is installed: no toolbars, no extras, no background services.", 2, 246, 560, 24, 10f, true);
        return page;
    }

    private Panel BuildLicence()
    {
        var page = new Panel();
        Caption(page, "By installing this software you agree to the Amanorsac Studio End User Licence Agreement. The full, current text is published here:", 2, 6, 560, 46, 10.5f);
        Link(page, "amanorsac.studio/legal", "https://amanorsac.studio/legal", 2, 56, 300);
        Caption(page, "How we handle your data:", 2, 92, 300, 22, 10.5f);
        Link(page, "amanorsac.studio/privacy", "https://amanorsac.studio/privacy", 2, 116, 300);

        var box = new Panel { BackColor = Panel };
        box.SetBounds(2, 158, 560, 128);
        page.Controls.Add(box);
        var heading = BuildInfo.Licensed ? "This product needs a licence key" : "No account, no activation";
        var body = BuildInfo.Licensed
            ? "The first plug-in you open will ask for your key, once. It unlocks the whole bundle on this computer. Your key is in your account under My Apps at amanorsac.studio, and in Amanorsac Hub. One key covers two computers."
            : "There is no key and no account. This product never connects to the internet.";
        var headingLabel = new Label { Text = heading, Font = UiFont(10.5f, FontStyle.Bold), ForeColor = Accent, BackColor = Color.Transparent };
        headingLabel.SetBounds(16, 12, 528, 24);
        var bodyLabel = new Label { Text = body, Font = UiFont(10f), ForeColor = TextMain, BackColor = Color.Transparent };
        bodyLabel.SetBounds(16, 40, 528, 80);
        box.Controls.Add(headingLabel);
        box.Controls.Add(bodyLabel);

        Style(acceptBox, "I accept the licence agreement");
        acceptBox.SetBounds(2, 312, 400, 28);
        acceptBox.CheckedChanged += (_, _) => RefreshButtons();
        page.Controls.Add(acceptBox);
        return page;
    }

    private Panel BuildComponents()
    {
        var page = new Panel();

        Style(vst3Box, $"VST3 plug-ins  ({BuildInfo.PluginCount})");
        vst3Box.Checked = true;
        vst3Box.SetBounds(2, 0, 400, 28);
        page.Controls.Add(vst3Box);
        var vst3Path = new TextBox { Text = InstallEngine.StandardVst3Root + "\\", ReadOnly = true };
        Style(vst3Path);
        vst3Path.ForeColor = TextDim;
        vst3Path.SetBounds(24, 32, 536, 24);
        page.Controls.Add(vst3Path);
        Caption(page, "The standard folder every DAW scans, so it cannot be changed.", 24, 60, 536, 20, 9f, true);

        Style(appsBox, $"Standalone applications  ({BuildInfo.PluginCount})");
        appsBox.Checked = true;
        appsBox.SetBounds(2, 96, 400, 28);
        page.Controls.Add(appsBox);
        Style(appRoot);
        appRoot.Text = InstallEngine.DefaultAppRoot;
        appRoot.SetBounds(24, 128, 440, 24);
        page.Controls.Add(appRoot);
        page.Controls.Add(BrowseButton(appRoot, 472, 126));

        Caption(page, "Also copy the plug-ins to a second folder  (optional)", 2, 176, 540, 24, 10.5f, false, FontStyle.Bold);
        Style(extraRoot);
        extraRoot.PlaceholderText = "Leave empty unless you keep a mirrored or portable plug-in folder";
        extraRoot.SetBounds(24, 204, 440, 24);
        page.Controls.Add(extraRoot);
        page.Controls.Add(BrowseButton(extraRoot, 472, 202));

        componentsNote.Font = UiFont(9.5f);
        componentsNote.ForeColor = TextDim;
        componentsNote.BackColor = Color.Transparent;
        componentsNote.SetBounds(2, 258, 560, 96);
        componentsNote.Text = "Windows will ask for permission once when you press Install. These folders are shared by every user of this computer, so Windows protects them. The installer asks for nothing else.\r\n\r\nYour own presets and settings are never stored in these folders.";
        page.Controls.Add(componentsNote);

        foreach (var box in new[] { vst3Box, appsBox }) box.CheckedChanged += (_, _) => RefreshButtons();
        appsBox.CheckedChanged += (_, _) => appRoot.Enabled = appsBox.Checked;
        return page;
    }

    private PillButton BrowseButton(TextBox target, int x, int y)
    {
        var button = new PillButton(false) { Text = "Browse", Accent = Accent, Font = UiFont(9f, FontStyle.Bold) };
        button.SetBounds(x, y, 88, 28);
        button.Click += (_, _) =>
        {
            using var dialog = new FolderBrowserDialog { UseDescriptionForTitle = true, Description = "Choose a folder" };
            if (Directory.Exists(target.Text)) dialog.SelectedPath = target.Text;
            if (dialog.ShowDialog(this) == DialogResult.OK) target.Text = dialog.SelectedPath;
        };
        return button;
    }

    private Panel BuildInstalling()
    {
        var page = new Panel();
        Caption(page, "Installing " + BuildInfo.ProductName, 2, 60, 560, 30, 13f, false, FontStyle.Bold);
        bar.SetBounds(2, 112, 560, 10);
        page.Controls.Add(bar);
        status.Font = UiFont(10f);
        status.ForeColor = TextDim;
        status.BackColor = Color.Transparent;
        status.SetBounds(2, 134, 560, 24);
        page.Controls.Add(status);
        return page;
    }

    private Panel BuildDone()
    {
        var page = new Panel();
        doneHeadline.Font = UiFont(13f, FontStyle.Bold);
        doneHeadline.ForeColor = TextMain;
        doneHeadline.BackColor = Color.Transparent;
        doneHeadline.SetBounds(2, 0, 560, 30);
        page.Controls.Add(doneHeadline);
        Caption(page, "What was installed, and where. Select any path to copy it into your DAW's plug-in scan settings.", 2, 34, 560, 40, 9.5f, true);

        Style(summary);
        summary.Multiline = true;
        summary.ReadOnly = true;
        summary.ScrollBars = ScrollBars.Vertical;
        summary.WordWrap = false;
        summary.SetBounds(2, 78, 560, 196);
        page.Controls.Add(summary);

        whatNext.Font = UiFont(10f);
        whatNext.ForeColor = TextMain;
        whatNext.BackColor = Color.Transparent;
        whatNext.SetBounds(2, 286, 560, 90);
        page.Controls.Add(whatNext);
        return page;
    }

    // ------------------------------------------------------------------ flow
    private void Show(int index)
    {
        current = Math.Clamp(index, 0, pages.Length - 1);
        for (var i = 0; i < pages.Length; ++i) pages[i].Visible = i == current;
        title.Text = StepNames[current];
        RefreshButtons();
        Invalidate();
    }

    private void RefreshButtons()
    {
        back.Visible = current is 1 or 2;
        cancel.Visible = current < 3;
        next.Visible = current != 3;
        next.Text = current switch { 2 => "Install", 4 => "Close", _ => "Next" };
        next.Enabled = current switch
        {
            1 => acceptBox.Checked,
            2 => vst3Box.Checked || appsBox.Checked,
            _ => true,
        };
    }

    private void Advance()
    {
        if (current == 4) { Close(); return; }
        if (current == 2) { BeginInstall(); return; }
        Show(current + 1);
    }

    private void BeginInstall()
    {
        var folder = Path.Combine(Path.GetTempPath(), "AmanorsacSetup_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        plan = new InstallPlan
        {
            InstallVst3 = vst3Box.Checked,
            InstallApps = appsBox.Checked,
            AppRoot = string.IsNullOrWhiteSpace(appRoot.Text) ? InstallEngine.DefaultAppRoot : appRoot.Text.Trim(),
            ExtraVst3Root = extraRoot.Text.Trim(),
            ProgressFile = Path.Combine(folder, "progress.txt"),
            ResultFile = Path.Combine(folder, "result.txt"),
        };
        var planPath = Path.Combine(folder, "plan.json");
        File.WriteAllText(planPath, JsonSerializer.Serialize(plan));

        try
        {
            // The one permission prompt, explained on the page the buyer is reading.
            worker = Process.Start(new ProcessStartInfo(Environment.ProcessPath!)
            {
                Arguments = $"--apply \"{planPath}\"",
                UseShellExecute = true,
                Verb = "runas",
            });
        }
        catch (Win32Exception)
        {
            componentsNote.ForeColor = Accent;
            componentsNote.Text = "Windows permission was not given, so nothing was installed. Press Install to try again; the installer needs it only to write into the shared plug-in folders.";
            return;
        }

        Show(3);
        bar.Value = 0;
        status.Text = "Starting";
        poll.Start();
    }

    private void PollWorker()
    {
        if (plan is null) return;
        try
        {
            if (File.Exists(plan.ProgressFile))
            {
                var parts = File.ReadAllText(plan.ProgressFile).Split('|', 2);
                if (int.TryParse(parts[0], out var percent)) bar.Value = percent;
                if (parts.Length > 1) status.Text = parts[1];
            }
        }
        catch (IOException) { /* the worker is mid-write; next tick */ }

        if (worker is null || !worker.HasExited) return;
        poll.Stop();
        Finish();
    }

    private void Finish()
    {
        var lines = plan is not null && File.Exists(plan.ResultFile) ? File.ReadAllLines(plan.ResultFile) : Array.Empty<string>();
        var succeeded = lines.Length > 0 && lines[0] == "ok";

        if (succeeded)
        {
            var items = lines.Skip(1).Select(l => l.Split('|', 2)).Where(p => p.Length == 2).ToList();
            doneHeadline.Text = BuildInfo.ProductName + " is installed";
            summary.Text = string.Join("\r\n", items.GroupBy(p => p[0]).SelectMany(group =>
                new[] { group.Key.ToUpperInvariant() }.Concat(group.Select(p => "  " + p[1])).Append("")));
            whatNext.Text = (BuildInfo.Licensed
                ? "What next: open any of the plug-ins and enter your licence key once. It is in your account under My Apps at amanorsac.studio."
                : "What next: open your DAW. There is no key to enter.")
                + "\r\nIf your DAW does not list the plug-ins, run a plug-in rescan in its preferences.";
        }
        else
        {
            doneHeadline.Text = "Installation did not finish";
            summary.Text = lines.Length > 1 ? string.Join("\r\n", lines.Skip(1)) : "The installer stopped before it could report a result.";
            whatNext.Text = "Nothing is left half-registered. You can run this installer again. If it keeps failing, write to hello@amanorsac.studio with the message above.";
        }

        try { if (plan is not null) Directory.Delete(Path.GetDirectoryName(plan.ResultFile)!, true); } catch { /* temp is best effort */ }
        Show(4);
    }

    /// <summary>Writes a picture of every page, filled with representative
    /// content, so a delivery can show what the buyer will see.</summary>
    public void CapturePages(string folder)
    {
        Directory.CreateDirectory(folder);
        // Child controls only paint once the window is really shown, so show it
        // off-screen rather than on top of whoever is running the build.
        StartPosition = FormStartPosition.Manual;
        Location = new Point(-20000, -20000);
        ShowInTaskbar = false;
        base.Show();
        Application.DoEvents();
        acceptBox.Checked = true;
        bar.Value = 62;
        status.Text = "Installing plug-ins";
        doneHeadline.Text = BuildInfo.ProductName + " is installed";
        summary.Text = "VST3\r\n  " + Path.Combine(InstallEngine.StandardVst3Root, "EXAMPLE PLUG-IN.vst3")
                     + "\r\n\r\nAPPLICATION\r\n  " + Path.Combine(InstallEngine.DefaultAppRoot, "EXAMPLE PLUG-IN.exe")
                     + "\r\n\r\nREAD ME\r\n  " + Path.Combine(InstallEngine.DefaultAppRoot, "README.txt");
        whatNext.Text = BuildInfo.Licensed
            ? "What next: open any of the plug-ins and enter your licence key once. It is in your account under My Apps at amanorsac.studio.\r\nIf your DAW does not list the plug-ins, run a plug-in rescan in its preferences."
            : "What next: open your DAW. There is no key to enter.";

        for (var i = 0; i < pages.Length; ++i)
        {
            Show(i);
            Refresh();
            Application.DoEvents();
            using var bitmap = new Bitmap(ClientSize.Width, ClientSize.Height);
            DrawToBitmap(bitmap, new Rectangle(Point.Empty, ClientSize));
            bitmap.Save(Path.Combine(folder, $"{i + 1}-{StepNames[i]}.png"), System.Drawing.Imaging.ImageFormat.Png);
        }
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        // Closing mid-install would leave the buyer guessing; let it finish.
        if (current == 3) e.Cancel = true;
        base.OnFormClosing(e);
    }

    // ------------------------------------------------------------------ owner-drawn controls
    private sealed class PillButton : Button
    {
        private readonly bool primary;
        public Color Accent { get; set; } = Color.White;

        public PillButton(bool isPrimary)
        {
            primary = isPrimary;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
            Cursor = Cursors.Hand;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent?.BackColor ?? Ground);
            var hover = ClientRectangle.Contains(PointToClient(Cursor.Position));
            var fill = !Enabled ? Line : primary ? (hover ? ControlPaint.Light(Accent, 0.25f) : Accent) : (hover ? Line : Panel);
            var rect = new Rectangle(0, 0, Width - 1, Height - 1);
            using var path = Rounded(rect, 8);
            using (var brush = new SolidBrush(fill)) g.FillPath(brush, path);
            if (!primary) using (var pen = new Pen(Line)) g.DrawPath(pen, path);
            var textColour = !Enabled ? TextDim : primary ? Color.FromArgb(0x0B, 0x0D, 0x10) : TextMain;
            TextRenderer.DrawText(g, Text, Font, rect, textColour, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        }

        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); Invalidate(); }
    }

    /// <summary>A checkbox drawn in the brand: an accent tile with a dark tick.</summary>
    private sealed class AccentCheck : CheckBox
    {
        public Color Accent { get; set; } = Color.White;

        public AccentCheck()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
            Cursor = Cursors.Hand;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent?.BackColor ?? Ground);
            var box = new Rectangle(1, (Height - 18) / 2, 18, 18);
            using var path = Rounded(box, 4);
            if (Checked)
            {
                using var fill = new SolidBrush(Enabled ? Accent : Line);
                g.FillPath(fill, path);
                using var tick = new Pen(Color.FromArgb(0x0B, 0x0D, 0x10), 2.2f) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round };
                g.DrawLines(tick, new[] { new PointF(box.X + 4.5f, box.Y + 9.5f), new PointF(box.X + 8f, box.Y + 13f), new PointF(box.X + 14f, box.Y + 5.5f) });
            }
            else
            {
                using var fill = new SolidBrush(Panel);
                g.FillPath(fill, path);
                using var pen = new Pen(Line, 1.5f);
                g.DrawPath(pen, path);
            }
            TextRenderer.DrawText(g, Text, Font, new Rectangle(28, 0, Width - 28, Height), Enabled ? TextMain : TextDim,
                                  TextFormatFlags.Left | TextFormatFlags.VerticalCenter);
        }
    }

    private sealed class AccentBar : Control
    {
        private readonly Color accent, track;
        private int value;
        public int Value { get => value; set { this.value = Math.Clamp(value, 0, 100); Invalidate(); } }

        public AccentBar(Color accentColour, Color trackColour)
        {
            accent = accentColour;
            track = trackColour;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent?.BackColor ?? Ground);
            using (var brush = new SolidBrush(track)) using (var path = Rounded(new Rectangle(0, 0, Width - 1, Height - 1), Height / 2)) g.FillPath(brush, path);
            var width = (int)((Width - 1) * value / 100.0);
            if (width < Height) return;
            using var fill = new SolidBrush(accent);
            using var done = Rounded(new Rectangle(0, 0, width, Height - 1), Height / 2);
            g.FillPath(fill, done);
        }
    }

    private static GraphicsPath Rounded(Rectangle r, int radius)
    {
        var d = Math.Max(2, radius * 2);
        var path = new GraphicsPath();
        path.AddArc(r.X, r.Y, d, d, 180, 90);
        path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        path.CloseFigure();
        return path;
    }
}
