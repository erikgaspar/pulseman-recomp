// Instalador do Pulseman para Windows (WPF, .NET Framework 4.8, que vem no Windows 10/11).
//
//   Pulseman-Setup.exe                    instala (escolhe a ROM, a pasta e os atalhos)
//   Desinstalar.exe /uninstall [pasta]    remove (copiado para a pasta do jogo na instalação)
//
// Os arquivos do jogo vão dentro do .exe como recursos "payload/..." (installer/build.ps1).
// A ROM não vai: o jogador escolhe a dele, que é conferida pelo SHA-1 e copiada como
// Pulseman_Japan.md ao lado do pulseman.exe. A interface está em ui.xaml.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using Microsoft.Win32;
using ShapePath = System.Windows.Shapes.Path;

namespace PulsemanSetup
{
    static class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            // desinstala com /uninstall (como o Windows chama) ou quando o próprio arquivo é o
            // Desinstalar.exe da pasta do jogo (aberto com dois cliques, sem argumentos)
            string selfName = System.IO.Path.GetFileNameWithoutExtension(Assembly.GetExecutingAssembly().Location);
            bool uninstall = args.Any(a => a.Equals("/uninstall", StringComparison.OrdinalIgnoreCase)) ||
                             selfName.Equals("Desinstalar", StringComparison.OrdinalIgnoreCase);
            // testes: /shot PÁGINA arq.png [rom] [idioma]  e  /selftest rom pasta
            if (args.Length >= 2 && args[0] == "/unselftest") {   // desinstala sem perguntar (teste)
                var testApp = new Application();
                var test = new SetupWindow(true, args[1]);
                test.RunUninstallTest();
                testApp.Run(test.Window);
                return 0;
            }
            if (args.Length >= 3 && (args[0] == "/shot" || args[0] == "/selftest")) {
                var testApp = new Application();
                var test = new SetupWindow(args[0] == "/shot" && args[1] == "PageUninstall", null);
                test.RunTest(args);
                testApp.Run(test.Window);
                return 0;
            }
            string dir = args.FirstOrDefault(a => !a.StartsWith("/"));
            if (uninstall && dir == null) {
                // rodando de dentro da pasta do jogo: copia para a pasta temporária e roda de lá,
                // senão o próprio desinstalador não poderia ser apagado
                dir = System.IO.Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
                string temp = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "Pulseman-Desinstalar.exe");
                try {
                    File.Copy(Assembly.GetExecutingAssembly().Location, temp, true);
                    Process.Start(new ProcessStartInfo(temp, "/uninstall \"" + dir + "\"") { UseShellExecute = false });
                    return 0;
                } catch (Exception) {
                    // sem cópia: desinstala daqui mesmo (o desinstalador fica para trás)
                }
            }
            var app = new Application();
            var ui = new SetupWindow(uninstall, dir);
            app.Run(ui.Window);
            return 0;
        }
    }

    class SetupWindow
    {
        public readonly Window Window;
        readonly bool uninstallMode;
        int lang;                    // 0 português, 1 english, 2 español (o mesmo do jogo)
        string installDir;
        byte[] romData;              // ROM conferida, pronta para gravar
        string currentPage;
        bool busy;

        const string RegKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\PulsemanRecomp";
        const string RomName = "Pulseman_Japan.md";
        const string UninstallerName = "Desinstalar.exe";

        // ---------- textos (português, english, español) ----------

        static readonly Dictionary<string, string[]> Texts = new Dictionary<string, string[]> {
            { "installer", new[] { "Instalador", "Setup", "Instalador" } },
            { "subtitle", new[] { "Recompilação para PC", "PC recompilation", "Recompilación para PC" } },
            { "tagline", new[] { "O clássico do Mega Drive rodando nativo no Windows, em widescreen e a 60 quadros por segundo.",
                                 "The Mega Drive classic running natively on Windows, in widescreen at 60 frames per second.",
                                 "El clásico de Mega Drive ejecutándose de forma nativa en Windows, en panorámico y a 60 fotogramas por segundo." } },
            { "step_rom", new[] { "ROM", "ROM", "ROM" } },
            { "step_install", new[] { "Instalação", "Install", "Instalación" } },
            { "step_done", new[] { "Pronto", "Done", "Listo" } },
            { "welcome_title", new[] { "Bem-vindo", "Welcome", "Bienvenido" } },
            { "welcome_body", new[] { "Este assistente instala o Pulseman no seu computador. Você só vai precisar da ROM original do jogo (versão japonesa).",
                                      "This wizard installs Pulseman on your computer. All you need is the original game ROM (Japanese version).",
                                      "Este asistente instala Pulseman en tu equipo. Solo necesitas la ROM original del juego (versión japonesa)." } },
            { "feat1", new[] { "Widescreen 16:9", "16:9 widescreen", "Panorámico 16:9" } },
            { "feat1b", new[] { "Mais cenário dos lados, ou o modo original", "More of the level on the sides, or the original mode", "Más escenario a los lados, o el modo original" } },
            { "feat2", new[] { "60 fps sem lentidão", "60 fps, no slowdown", "60 fps sin ralentización" } },
            { "feat2b", new[] { "Filtros de TV, scanlines e tela cheia", "TV filters, scanlines and full screen", "Filtros de TV, scanlines y pantalla completa" } },
            { "feat3", new[] { "3 arquivos de save", "3 save files", "3 archivos de guardado" } },
            { "feat3b", new[] { "Salve e continue de qualquer ponto", "Save and continue from anywhere", "Guarda y continúa desde cualquier punto" } },
            { "start", new[] { "Começar", "Get started", "Empezar" } },
            { "rom_title", new[] { "Escolha a ROM", "Choose the ROM", "Elige la ROM" } },
            { "rom_body", new[] { "O jogo precisa da ROM original do Pulseman (Japão). Ela é conferida e copiada só para a pasta do jogo.",
                                  "The game needs the original Pulseman ROM (Japan). It is verified and copied only into the game folder.",
                                  "El juego necesita la ROM original de Pulseman (Japón). Se verifica y se copia solo a la carpeta del juego." } },
            { "drop", new[] { "Arraste a ROM para cá", "Drag the ROM here", "Arrastra la ROM aquí" } },
            { "browse", new[] { "ou clique para procurar (.md, .bin, .gen, .smd ou .zip)", "or click to browse (.md, .bin, .gen, .smd or .zip)",
                                "o haz clic para buscar (.md, .bin, .gen, .smd o .zip)" } },
            { "rom_ok", new[] { "ROM verificada", "ROM verified", "ROM verificada" } },
            { "rom_ok_detail", new[] { "Pulseman (Japão) · original", "Pulseman (Japan) · original", "Pulseman (Japón) · original" } },
            { "rom_installed", new[] { "ROM já instalada", "ROM already installed", "ROM ya instalada" } },
            { "rom_bad", new[] { "ROM não reconhecida", "ROM not recognized", "ROM no reconocida" } },
            { "rom_bad_detail", new[] { "Precisa ser a ROM japonesa original do Pulseman, sem modificações.",
                                        "It must be the original, unmodified Japanese Pulseman ROM.",
                                        "Debe ser la ROM japonesa original de Pulseman, sin modificaciones." } },
            { "rom_read_error", new[] { "Não foi possível ler o arquivo", "Could not read the file", "No se pudo leer el archivo" } },
            { "rom_dialog", new[] { "Escolha a ROM do Pulseman (Japão)", "Choose the Pulseman (Japan) ROM", "Elige la ROM de Pulseman (Japón)" } },
            { "rom_filter", new[] { "ROMs de Mega Drive", "Mega Drive ROMs", "ROMs de Mega Drive" } },
            { "all_files", new[] { "Todos os arquivos", "All files", "Todos los archivos" } },
            { "back", new[] { "Voltar", "Back", "Volver" } },
            { "next", new[] { "Continuar", "Continue", "Continuar" } },
            { "opt_title", new[] { "Onde instalar", "Where to install", "Dónde instalar" } },
            { "opt_body", new[] { "A pasta padrão é só do seu usuário e não pede permissão de administrador.",
                                  "The default folder belongs to your user only and does not need administrator rights.",
                                  "La carpeta predeterminada es solo de tu usuario y no requiere permisos de administrador." } },
            { "change", new[] { "Alterar", "Change", "Cambiar" } },
            { "space", new[] { "Espaço necessário: {0}", "Space required: {0}", "Espacio necesario: {0}" } },
            { "folder_dialog", new[] { "Escolha a pasta de instalação do Pulseman", "Choose the Pulseman install folder", "Elige la carpeta de instalación de Pulseman" } },
            { "opt_desktop", new[] { "Atalho na área de trabalho", "Desktop shortcut", "Acceso directo en el escritorio" } },
            { "opt_start", new[] { "Atalho no menu Iniciar", "Start menu shortcut", "Acceso directo en el menú Inicio" } },
            { "opt_launch", new[] { "Abrir o jogo ao terminar", "Launch the game when done", "Abrir el juego al terminar" } },
            { "install", new[] { "Instalar", "Install", "Instalar" } },
            { "installing", new[] { "Instalando…", "Installing…", "Instalando…" } },
            { "st_files", new[] { "Copiando os arquivos do jogo…", "Copying game files…", "Copiando los archivos del juego…" } },
            { "st_rom", new[] { "Copiando a ROM…", "Copying the ROM…", "Copiando la ROM…" } },
            { "st_shortcuts", new[] { "Criando os atalhos…", "Creating shortcuts…", "Creando los accesos directos…" } },
            { "st_register", new[] { "Registrando no Windows…", "Registering with Windows…", "Registrando en Windows…" } },
            { "done_title", new[] { "Tudo pronto!", "All set!", "¡Todo listo!" } },
            { "done_body", new[] { "O Pulseman foi instalado. Esc ou Start abrem o menu durante o jogo.",
                                   "Pulseman has been installed. Esc or Start opens the menu while playing.",
                                   "Pulseman se ha instalado. Esc o Start abren el menú durante el juego." } },
            { "play", new[] { "Jogar agora", "Play now", "Jugar ahora" } },
            { "close", new[] { "Fechar", "Close", "Cerrar" } },
            { "cancel", new[] { "Cancelar", "Cancel", "Cancelar" } },
            { "un_title", new[] { "Desinstalar o Pulseman", "Uninstall Pulseman", "Desinstalar Pulseman" } },
            { "un_body", new[] { "O jogo, a ROM copiada e os atalhos serão removidos desta pasta:",
                                 "The game, the copied ROM and the shortcuts will be removed from this folder:",
                                 "El juego, la ROM copiada y los accesos directos se eliminarán de esta carpeta:" } },
            { "un_keep", new[] { "Manter os jogos salvos e as configurações", "Keep saved games and settings", "Conservar las partidas guardadas y la configuración" } },
            { "uninstall", new[] { "Desinstalar", "Uninstall", "Desinstalar" } },
            { "uninstalling", new[] { "Removendo…", "Removing…", "Eliminando…" } },
            { "un_done_title", new[] { "Pulseman removido", "Pulseman removed", "Pulseman eliminado" } },
            { "un_done_body", new[] { "O jogo foi desinstalado deste computador.", "The game has been uninstalled from this computer.",
                                      "El juego se ha desinstalado de este equipo." } },
            { "info_link", new[] { "Por que preciso da ROM?", "Why do I need the ROM?", "¿Por qué necesito la ROM?" } },
            { "info_title", new[] { "Sobre o jogo e a ROM", "About the game and the ROM", "Sobre el juego y la ROM" } },
            { "info_body1", new[] {
                "Este port não inclui nenhum arquivo do jogo original. O instalador traz apenas o programa que roda o jogo no PC; todo o conteúdo do Pulseman (código, gráficos, músicas e sons) é lido da ROM que você fornece.",
                "This port does not include any files from the original game. The installer only contains the program that runs the game on PC; all Pulseman content (code, graphics, music and sound) is read from the ROM you provide.",
                "Este port no incluye ningún archivo del juego original. El instalador solo trae el programa que ejecuta el juego en PC; todo el contenido de Pulseman (código, gráficos, música y sonidos) se lee de la ROM que proporcionas." } },
            { "info_body2", new[] {
                "Para jogar, você precisa da ROM original do Pulseman (versão japonesa, sem modificações), obtida a partir de um cartucho que você possua. Ela é conferida e copiada só para a pasta do jogo, no seu computador.",
                "To play, you need the original Pulseman ROM (Japanese version, unmodified), obtained from a cartridge you own. It is verified and copied only into the game folder on your computer.",
                "Para jugar, necesitas la ROM original de Pulseman (versión japonesa, sin modificaciones), obtenida de un cartucho que poseas. Se verifica y se copia solo a la carpeta del juego, en tu equipo." } },
            { "info_body3", new[] {
                "Este projeto não fornece a ROM e não indica onde baixá-la.",
                "This project does not provide the ROM and does not say where to download it.",
                "Este proyecto no proporciona la ROM ni indica dónde descargarla." } },
            { "info_ok", new[] { "Entendi", "Got it", "Entendido" } },
            { "info_tip", new[] { "Sobre o jogo e a ROM", "About the game and the ROM", "Sobre el juego y la ROM" } },
            { "err_title", new[] { "Algo deu errado", "Something went wrong", "Algo salió mal" } },
            { "err_running", new[] { "O Pulseman está aberto. Feche o jogo e tente de novo.", "Pulseman is running. Close the game and try again.",
                                     "Pulseman está abierto. Cierra el juego e inténtalo de nuevo." } },
            { "err_access", new[] { "Sem permissão para gravar nesta pasta. Escolha outra pasta (por exemplo, a sugerida).",
                                    "No permission to write to this folder. Choose another one (for example, the suggested one).",
                                    "Sin permiso para escribir en esta carpeta. Elige otra (por ejemplo, la sugerida)." } },
        };

        string T(string key)
        {
            string[] v;
            return Texts.TryGetValue(key, out v) ? v[lang] : key;
        }

        T_ Find<T_>(string name) where T_ : class
        {
            return Window.FindName(name) as T_;
        }

        // ---------- montagem ----------

        public SetupWindow(bool uninstall, string dir)
        {
            uninstallMode = uninstall;
            string culture = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
            lang = culture == "pt" ? 0 : culture == "es" ? 2 : 1;

            using (var s = Resource("ui.xaml"))
                Window = (Window)XamlReader.Load(s);
            LoadIcons();
            Find<TextBlock>("VersionText").Text = "v" + Build.Version + " · Shin Gattai Ports";

            var shell = Find<Grid>("Shell");
            shell.SizeChanged += (s, e) => shell.Clip = new RectangleGeometry(new Rect(0, 0, shell.ActualWidth, shell.ActualHeight), 18, 18);
            Find<Grid>("TitleBar").MouseLeftButtonDown += (s, e) => { if (e.ButtonState == MouseButtonState.Pressed) Window.DragMove(); };
            Find<Button>("MinButton").Click += (s, e) => Window.WindowState = WindowState.Minimized;
            Find<Button>("CloseButton").Click += (s, e) => { if (!busy) Window.Close(); };
            Find<Button>("InfoButton").Click += (s, e) => ShowInfo(true);
            Find<Button>("InfoLink").Click += (s, e) => ShowInfo(true);
            Find<Button>("InfoOk").Click += (s, e) => ShowInfo(false);
            var overlay = Find<Grid>("InfoOverlay");
            overlay.MouseLeftButtonDown += (s, e) => { if (e.OriginalSource == overlay) ShowInfo(false); };   // clique fora do cartão
            Window.KeyDown += (s, e) => { if (e.Key == Key.Escape && overlay.Visibility == Visibility.Visible) ShowInfo(false); };
            Find<Button>("LangPt").Click += (s, e) => SetLanguage(0);
            Find<Button>("LangEn").Click += (s, e) => SetLanguage(1);
            Find<Button>("LangEs").Click += (s, e) => SetLanguage(2);

            SetupBackground();
            WireInstallPages();
            WireUninstallPage();

            installDir = dir ?? ExistingInstallDir() ?? DefaultInstallDir();
            SetLanguage(lang);
            ShowPage(uninstallMode ? "PageUninstall" : "PageWelcome");
        }

        static Stream Resource(string name)
        {
            return Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
        }

        // ícone da janela (32 px) e o da barra de título (o maior, reduzido: fica nítido)
        void LoadIcons()
        {
            try {
                var dec = BitmapDecoder.Create(Resource("icon.ico"), BitmapCreateOptions.None, BitmapCacheOption.OnLoad);
                Window.Icon = dec.Frames.OrderBy(f => Math.Abs(f.PixelWidth - 32)).First();
                Find<Image>("AppIcon").Source = dec.Frames.OrderByDescending(f => f.PixelWidth).First();
            } catch (Exception) { }
        }

        void SetLanguage(int l)
        {
            lang = l;
            Find<Button>("LangPt").Tag = l == 0 ? "on" : null;
            Find<Button>("LangEn").Tag = l == 1 ? "on" : null;
            Find<Button>("LangEs").Tag = l == 2 ? "on" : null;
            ApplyTexts(Window);
            Find<Button>("InfoButton").ToolTip = T("info_tip");
            Find<TextBlock>("FolderText").Text = installDir;
            Find<TextBlock>("UnFolderText").Text = installDir;
            Find<TextBlock>("SpaceText").Text = string.Format(T("space"), FormatSize(PayloadSize() + 2 * 1024 * 1024));
            UpdateSteps();
            if (romData != null || Find<Border>("RomStatus").Visibility == Visibility.Visible)
                RefreshRomStatus();
        }

        // Janela de informações: o port não traz nada do jogo; a ROM é do jogador
        void ShowInfo(bool show)
        {
            var overlay = Find<Grid>("InfoOverlay");
            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            if (show) {
                overlay.Visibility = Visibility.Visible;
                overlay.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(200)));
                var scale = Find<ScaleTransform>("InfoScale");
                var pop = new DoubleAnimation(0.94, 1, TimeSpan.FromMilliseconds(260)) { EasingFunction = ease };
                scale.BeginAnimation(ScaleTransform.ScaleXProperty, pop);
                scale.BeginAnimation(ScaleTransform.ScaleYProperty, pop);
                Find<Button>("InfoOk").Focus();
            } else {
                var fade = new DoubleAnimation(1, 0, TimeSpan.FromMilliseconds(160));
                fade.Completed += (s, e) => overlay.Visibility = Visibility.Collapsed;
                overlay.BeginAnimation(UIElement.OpacityProperty, fade);
            }
        }

        // Tag="t:chave" vira o texto do elemento (TextBlock, botão ou chave)
        void ApplyTexts(DependencyObject root)
        {
            var fe = root as FrameworkElement;
            if (fe != null) {
                string tag = fe.Tag as string;
                if (tag != null && tag.StartsWith("t:")) {
                    string text = T(tag.Substring(2));
                    if (fe is TextBlock) ((TextBlock)fe).Text = text;
                    else if (fe is ContentControl) ((ContentControl)fe).Content = text;
                }
            }
            foreach (object child in LogicalTreeHelper.GetChildren(root)) {
                var d = child as DependencyObject;
                if (d != null)
                    ApplyTexts(d);
            }
        }

        // ---------- fundo ----------

        void SetupBackground()
        {
            Stream wall = Resource("wallpaper");
            if (wall != null) {
                var img = new BitmapImage();
                img.BeginInit();
                img.StreamSource = wall;
                img.CacheOption = BitmapCacheOption.OnLoad;
                img.EndInit();
                var image = Find<Image>("Wallpaper");
                image.Source = img;
                image.Visibility = Visibility.Visible;
                Find<Grid>("FallbackBg").Visibility = Visibility.Collapsed;
                // aproximação bem lenta, para o fundo não ficar parado
                var zoom = Find<ScaleTransform>("WallpaperZoom");
                var anim = new DoubleAnimation(1.0, 1.045, TimeSpan.FromSeconds(30)) {
                    AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever,
                    EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
                };
                zoom.BeginAnimation(ScaleTransform.ScaleXProperty, anim);
                zoom.BeginAnimation(ScaleTransform.ScaleYProperty, anim);
                return;
            }
            // sem papel de parede: ondas elétricas animadas, como no menu do jogo
            var canvas = Find<Canvas>("Waves");
            var lines = new List<Polyline>();
            Color[] colors = { Color.FromArgb(0x80, 0xFF, 0x4F, 0xA6), Color.FromArgb(0x50, 0xB0, 0x5C, 0xFF), Color.FromArgb(0x38, 0xFF, 0x4F, 0xA6) };
            for (int i = 0; i < 3; i++) {
                var p = new Polyline { Stroke = new SolidColorBrush(colors[i]), StrokeThickness = i == 0 ? 2.2 : 1.4 };
                p.Effect = new System.Windows.Media.Effects.BlurEffect { Radius = i == 0 ? 2 : 1 };
                canvas.Children.Add(p);
                lines.Add(p);
            }
            var start = DateTime.Now;
            var timer = new DispatcherTimer(DispatcherPriority.Render) { Interval = TimeSpan.FromMilliseconds(33) };
            timer.Tick += (s, e) => {
                double w = canvas.ActualWidth, h = canvas.ActualHeight, t = (DateTime.Now - start).TotalSeconds;
                if (w <= 0) return;
                for (int i = 0; i < lines.Count; i++) {
                    var pts = new PointCollection();
                    double baseY = h * (0.62 + 0.09 * i);
                    for (double x = 0; x <= w; x += 6) {
                        double env = Math.Sin(x / w * Math.PI);
                        double y = baseY + env * (Math.Sin(x * 0.012 - t * (0.9 + 0.3 * i)) * (26 - 6 * i) +
                                                  Math.Sin(x * 0.041 + t * (2.1 + i)) * 6);
                        pts.Add(new Point(x, y));
                    }
                    lines[i].Points = pts;
                }
            };
            timer.Start();
        }

        // ---------- páginas ----------

        void ShowPage(string name)
        {
            foreach (string p in new[] { "PageWelcome", "PageRom", "PageOptions", "PageProgress", "PageDone", "PageUninstall", "PageError" })
                Find<Grid>(p).Visibility = p == name ? Visibility.Visible : Visibility.Collapsed;
            currentPage = name;
            var page = Find<Grid>(name);
            var move = new TranslateTransform(28, 0);
            page.RenderTransform = move;
            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            move.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(28, 0, TimeSpan.FromMilliseconds(320)) { EasingFunction = ease });
            page.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(260)));
            UpdateSteps();
        }

        void UpdateSteps()
        {
            int step = currentPage == "PageRom" ? 1 : currentPage == "PageOptions" || currentPage == "PageProgress" ? 2 :
                       currentPage == "PageDone" ? 3 : 0;
            Find<StackPanel>("Steps").Visibility = uninstallMode ? Visibility.Collapsed : Visibility.Visible;
            for (int i = 1; i <= 3; i++) {
                var dot = Find<Border>("Step" + i + "Dot");
                var num = Find<TextBlock>("Step" + i + "Num");
                var text = Find<TextBlock>("Step" + i + "Text");
                bool done = i < step || (step == 3 && i == 3), now = i == step && !done;
                dot.Background = now || done ? (Brush)Window.Resources["PrimaryFill"] : new SolidColorBrush(Color.FromArgb(0x22, 0xFF, 0xFF, 0xFF));
                dot.BorderBrush = new SolidColorBrush(Color.FromArgb(0x40, 0xFF, 0xFF, 0xFF));
                dot.BorderThickness = new Thickness(now || done ? 0 : 1);
                num.Text = done ? "✓" : i.ToString();
                num.Foreground = now || done ? (Brush)Window.Resources["OnAccent"] : (Brush)Window.Resources["Soft"];
                text.Foreground = now ? (Brush)Window.Resources["Text"] : done ? (Brush)Window.Resources["Soft"] : (Brush)Window.Resources["Dim"];
                text.FontWeight = now ? FontWeights.SemiBold : FontWeights.Normal;
            }
        }

        void WireInstallPages()
        {
            Find<Button>("StartButton").Click += (s, e) => { CheckInstalledRom(); ShowPage("PageRom"); };
            Find<Button>("RomBack").Click += (s, e) => ShowPage("PageWelcome");
            Find<Button>("RomNext").Click += (s, e) => ShowPage("PageOptions");
            Find<Button>("OptBack").Click += (s, e) => ShowPage("PageRom");
            Find<Button>("InstallButton").Click += (s, e) => StartInstall();
            Find<Button>("FolderButton").Click += (s, e) => ChooseFolder();
            Find<Button>("DoneClose").Click += (s, e) => Window.Close();
            Find<Button>("PlayButton").Click += (s, e) => { LaunchGame(); Window.Close(); };
            Find<Button>("ErrClose").Click += (s, e) => Window.Close();
            Find<Button>("ErrBack").Click += (s, e) => ShowPage(uninstallMode ? "PageUninstall" : "PageOptions");

            var zone = Find<Grid>("DropZone");
            var border = Find<Rectangle>("DropBorder");
            zone.MouseLeftButtonUp += (s, e) => BrowseRom();
            Brush accent = (Brush)Window.Resources["Accent"], soft = (Brush)Window.Resources["AccentSoft"];
            Brush tint = (Brush)Window.Resources["AccentTint"], idle = new SolidColorBrush(Color.FromArgb(0x08, 0xFF, 0xFF, 0xFF));
            zone.MouseEnter += (s, e) => border.Stroke = accent;
            zone.MouseLeave += (s, e) => border.Stroke = soft;
            zone.DragEnter += (s, e) => { border.Stroke = accent; zone.Background = tint; };
            zone.DragLeave += (s, e) => { border.Stroke = soft; zone.Background = idle; };
            zone.Drop += (s, e) => {
                zone.Background = idle;
                border.Stroke = soft;
                var files = e.Data.GetData(DataFormats.FileDrop) as string[];
                if (files != null && files.Length > 0)
                    CheckRom(files[0]);
            };
        }

        // ---------- ROM ----------

        enum RomState { None, Ok, Installed, Bad, ReadError }
        RomState romState = RomState.None;
        string romFileName;

        void BrowseRom()
        {
            var dlg = new OpenFileDialog {
                Title = T("rom_dialog"),
                Filter = T("rom_filter") + " (*.md;*.bin;*.gen;*.smd;*.zip)|*.md;*.bin;*.gen;*.smd;*.zip|" + T("all_files") + " (*.*)|*.*"
            };
            if (dlg.ShowDialog(Window) == true)
                CheckRom(dlg.FileName);
        }

        static string Sha1(byte[] data)
        {
            using (var sha = SHA1.Create())
                return BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant();
        }

        // Aceita a ROM direta, em formato .smd (intercalado, com cabeçalho de 512 bytes) ou num .zip
        static byte[] MatchRom(byte[] data)
        {
            if (Sha1(data) == Build.RomSha1)
                return data;
            if (data.Length > 512 && (data.Length - 512) % 16384 == 0) {
                var plain = new byte[data.Length - 512];
                for (int b = 0; b < plain.Length / 16384; b++)
                    for (int i = 0; i < 8192; i++) {
                        plain[b * 16384 + i * 2 + 1] = data[512 + b * 16384 + i];
                        plain[b * 16384 + i * 2] = data[512 + b * 16384 + 8192 + i];
                    }
                if (Sha1(plain) == Build.RomSha1)
                    return plain;
            }
            return null;
        }

        void CheckRom(string path)
        {
            romFileName = System.IO.Path.GetFileName(path);
            romData = null;
            try {
                if (new FileInfo(path).Length > 16 * 1024 * 1024)
                    throw new InvalidDataException();
                if (path.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) {
                    using (var zip = ZipFile.OpenRead(path))
                        foreach (var entry in zip.Entries) {
                            if (entry.Length == 0 || entry.Length > 16 * 1024 * 1024)
                                continue;
                            using (var s = entry.Open())
                            using (var m = new MemoryStream()) {
                                s.CopyTo(m);
                                romData = MatchRom(m.ToArray());
                            }
                            if (romData != null) {
                                romFileName = entry.Name;
                                break;
                            }
                        }
                } else {
                    romData = MatchRom(File.ReadAllBytes(path));
                }
                romState = romData != null ? RomState.Ok : RomState.Bad;
            } catch (Exception) {
                romState = RomState.ReadError;
            }
            RefreshRomStatus();
        }

        // Reinstalação: a ROM que já está na pasta serve
        void CheckInstalledRom()
        {
            if (romData != null)
                return;
            string path = System.IO.Path.Combine(installDir, RomName);
            try {
                if (File.Exists(path) && MatchRom(File.ReadAllBytes(path)) != null) {
                    romData = File.ReadAllBytes(path);
                    romState = RomState.Installed;
                    romFileName = RomName;
                    RefreshRomStatus();
                }
            } catch (Exception) { }
        }

        void RefreshRomStatus()
        {
            var box = Find<Border>("RomStatus");
            if (romState == RomState.None) {
                box.Visibility = Visibility.Collapsed;
                return;
            }
            bool ok = romState == RomState.Ok || romState == RomState.Installed;
            box.Visibility = Visibility.Visible;
            Find<Border>("RomStatusDot").Background = ok ? (Brush)Window.Resources["PrimaryFill"] : (Brush)Window.Resources["DangerFill"];
            var icon = Find<ShapePath>("RomStatusIcon");
            icon.Data = Geometry.Parse(ok ? "M0,6 L5,11 L13,2" : "M0,0 L10,10 M10,0 L0,10");
            icon.Stroke = Brushes.White;
            Find<TextBlock>("RomStatusTitle").Text = romState == RomState.Ok ? T("rom_ok") : romState == RomState.Installed ? T("rom_installed") :
                                                     romState == RomState.Bad ? T("rom_bad") : T("rom_read_error");
            Find<TextBlock>("RomStatusDetail").Text = ok ? romFileName + " · " + T("rom_ok_detail") :
                                                      romState == RomState.Bad ? romFileName + " · " + T("rom_bad_detail") : romFileName;
            Find<Button>("RomNext").IsEnabled = ok;
            box.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(220)));
        }

        // ---------- pasta ----------

        static string DefaultInstallDir()
        {
            return System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Pulseman");
        }

        static string ExistingInstallDir()
        {
            using (var key = Registry.CurrentUser.OpenSubKey(RegKey)) {
                string dir = key == null ? null : key.GetValue("InstallLocation") as string;
                return dir != null && Directory.Exists(dir) ? dir : null;
            }
        }

        void ChooseFolder()
        {
            using (var dlg = new System.Windows.Forms.FolderBrowserDialog { Description = T("folder_dialog"), ShowNewFolderButton = true }) {
                dlg.SelectedPath = Directory.Exists(installDir) ? installDir : Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                if (dlg.ShowDialog() != System.Windows.Forms.DialogResult.OK)
                    return;
                installDir = dlg.SelectedPath;
                // uma pasta qualquer (ex.: Jogos) ganha a subpasta Pulseman
                if (!System.IO.Path.GetFileName(installDir).Equals("Pulseman", StringComparison.OrdinalIgnoreCase))
                    installDir = System.IO.Path.Combine(installDir, "Pulseman");
                Find<TextBlock>("FolderText").Text = installDir;
            }
        }

        static IEnumerable<string> PayloadNames()
        {
            return Assembly.GetExecutingAssembly().GetManifestResourceNames().Where(n => n.StartsWith("payload/"));
        }

        static long PayloadSize()
        {
            long total = 0;
            foreach (string n in PayloadNames())
                using (var s = Resource(n))
                    total += s.Length;
            return total;
        }

        static string FormatSize(long bytes)
        {
            return (bytes / (1024.0 * 1024.0)).ToString("0.0", CultureInfo.CurrentCulture) + " MB";
        }

        // ---------- instalação ----------

        void SetProgress(double fraction, string statusKey)
        {
            Window.Dispatcher.Invoke(new Action(() => {
                var track = Find<Border>("ProgressTrack");
                if (track.ActualWidth <= 0) {   // página ainda sem layout: tenta de novo logo depois
                    Window.Dispatcher.BeginInvoke(new Action(() => SetProgress(fraction, statusKey)), DispatcherPriority.Loaded);
                    return;
                }
                var fill = Find<Border>("ProgressFill");
                double target = track.ActualWidth * fraction;
                fill.BeginAnimation(FrameworkElement.WidthProperty,
                    new DoubleAnimation(target, TimeSpan.FromMilliseconds(260)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
                Find<TextBlock>("ProgressPercent").Text = ((int)(fraction * 100)) + "%";
                if (statusKey != null)
                    Find<TextBlock>("ProgressStatus").Text = T(statusKey);
            }));
        }

        void ResetProgress()
        {
            var fill = Find<Border>("ProgressFill");
            fill.BeginAnimation(FrameworkElement.WidthProperty, null);
            fill.Width = 0;
            Find<TextBlock>("ProgressPercent").Text = "0%";
        }

        void Fail(string message)
        {
            if (selfTest) {
                File.WriteAllText(System.IO.Path.Combine(installDir, "selftest.txt"), "erro: " + message);
                Window.Dispatcher.Invoke(new Action(() => Window.Close()));
                return;
            }
            Window.Dispatcher.Invoke(new Action(() => {
                busy = false;
                Find<TextBlock>("ErrorText").Text = message;
                ShowPage("PageError");
            }));
        }

        static bool GameRunning(string dir)
        {
            string exe = System.IO.Path.Combine(dir, "pulseman.exe");
            if (!File.Exists(exe))
                return false;
            try {
                using (File.Open(exe, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) { }
                return false;
            } catch (IOException) {
                return true;
            } catch (UnauthorizedAccessException) {
                return false;
            }
        }

        void StartInstall()
        {
            busy = true;
            Find<TextBlock>("ProgressTitle").Text = T("installing");
            ResetProgress();
            ShowPage("PageProgress");
            bool desktop = Find<CheckBox>("OptDesktop").IsChecked == true;
            bool startMenu = Find<CheckBox>("OptStart").IsChecked == true;
            string dir = installDir;
            byte[] rom = romData;
            int language = lang;
            new Thread(() => {
                try {
                    if (GameRunning(dir)) {
                        Fail(T("err_running"));
                        return;
                    }
                    Directory.CreateDirectory(dir);
                    var names = PayloadNames().ToList();
                    long total = PayloadSize() + rom.Length, doneBytes = 0;
                    SetProgress(0.02, "st_files");
                    foreach (string name in names) {
                        string target = System.IO.Path.Combine(dir, name.Substring("payload/".Length).Replace('/', '\\'));
                        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(target));
                        using (var src = Resource(name))
                        using (var dst = File.Create(target)) {
                            var buf = new byte[81920];
                            int n;
                            while ((n = src.Read(buf, 0, buf.Length)) > 0) {
                                dst.Write(buf, 0, n);
                                doneBytes += n;
                                SetProgress(0.85 * doneBytes / total, null);
                            }
                        }
                        Thread.Sleep(60);   // a barra anda de verdade, sem pular direto para o fim
                    }
                    SetProgress(0.86, "st_rom");
                    File.WriteAllBytes(System.IO.Path.Combine(dir, RomName), rom);
                    Thread.Sleep(150);
                    SetGameLanguage(System.IO.Path.Combine(dir, "pulseman.ini"), language);
                    File.Copy(Assembly.GetExecutingAssembly().Location, System.IO.Path.Combine(dir, UninstallerName), true);

                    SetProgress(0.92, "st_shortcuts");
                    string exe = System.IO.Path.Combine(dir, "pulseman.exe");
                    string startLink = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "Pulseman.lnk");
                    string deskLink = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Pulseman.lnk");
                    if (startMenu) CreateShortcut(startLink, exe, dir); else TryDelete(startLink);
                    if (desktop) CreateShortcut(deskLink, exe, dir); else TryDelete(deskLink);
                    Thread.Sleep(150);

                    SetProgress(0.97, "st_register");
                    if (!selfTest)
                        Register(dir, total);
                    Thread.Sleep(200);
                    SetProgress(1.0, null);
                    Thread.Sleep(350);
                    if (selfTest) {
                        File.WriteAllText(System.IO.Path.Combine(dir, "selftest.txt"), "ok");
                        Window.Dispatcher.Invoke(new Action(() => Window.Close()));
                        return;
                    }
                    Window.Dispatcher.Invoke(new Action(() => {
                        busy = false;
                        Find<TextBlock>("DoneTitle").Text = T("done_title");
                        Find<TextBlock>("DoneBody").Text = T("done_body");
                        Find<Button>("PlayButton").Visibility = Visibility.Visible;
                        ShowDone();
                        if (Find<CheckBox>("OptLaunch").IsChecked == true) {
                            // abre o jogo um instante depois, com a tela de "pronto" já à vista
                            var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(900) };
                            t.Tick += (s2, e2) => { t.Stop(); LaunchGame(); Window.Close(); };
                            t.Start();
                        }
                    }));
                } catch (UnauthorizedAccessException) {
                    Fail(T("err_access"));
                } catch (Exception ex) {
                    Fail(ex.Message);
                }
            }) { IsBackground = true }.Start();
        }

        // O idioma dos menus do jogo passa a ser o escolhido no instalador. Num pulseman.ini que
        // já existe (reinstalação), só a linha "idioma" muda; o resto das configurações fica.
        static void SetGameLanguage(string ini, int language)
        {
            var lines = File.Exists(ini) ? File.ReadAllLines(ini).ToList() : new List<string>();
            string entry = "idioma = " + language;
            int i = lines.FindIndex(l => System.Text.RegularExpressions.Regex.IsMatch(l, @"^\s*idioma\s*="));
            if (i >= 0) {
                lines[i] = entry;
            } else {
                lines.Add("# idioma dos menus: 0 = português, 1 = english, 2 = español");
                lines.Add(entry);
            }
            File.WriteAllText(ini, string.Join("\r\n", lines) + "\r\n");
        }

        void ShowDone()
        {
            ShowPage("PageDone");
            var scale = Find<ScaleTransform>("DoneScale");
            var pop = new DoubleAnimation(0.3, 1, TimeSpan.FromMilliseconds(520)) { EasingFunction = new BackEase { Amplitude = 0.5, EasingMode = EasingMode.EaseOut } };
            scale.BeginAnimation(ScaleTransform.ScaleXProperty, pop);
            scale.BeginAnimation(ScaleTransform.ScaleYProperty, pop);
        }

        static void CreateShortcut(string link, string target, string workDir)
        {
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(shellType);
            object sc = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { link });
            Type t = sc.GetType();
            t.InvokeMember("TargetPath", BindingFlags.SetProperty, null, sc, new object[] { target });
            t.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, sc, new object[] { workDir });
            t.InvokeMember("IconLocation", BindingFlags.SetProperty, null, sc, new object[] { target + ",0" });
            t.InvokeMember("Description", BindingFlags.SetProperty, null, sc, new object[] { "Pulseman" });
            t.InvokeMember("Save", BindingFlags.InvokeMethod, null, sc, null);
        }

        static void Register(string dir, long bytes)
        {
            using (var key = Registry.CurrentUser.CreateSubKey(RegKey)) {
                string un = System.IO.Path.Combine(dir, UninstallerName);
                key.SetValue("DisplayName", "Pulseman");
                key.SetValue("DisplayVersion", Build.Version);
                key.SetValue("Publisher", "Shin Gattai Ports");
                key.SetValue("DisplayIcon", System.IO.Path.Combine(dir, "pulseman.exe"));
                key.SetValue("InstallLocation", dir);
                key.SetValue("UninstallString", "\"" + un + "\" /uninstall");
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                key.SetValue("EstimatedSize", (int)(bytes / 1024), RegistryValueKind.DWord);
                key.SetValue("InstallDate", DateTime.Now.ToString("yyyyMMdd"));
            }
        }

        void LaunchGame()
        {
            try {
                string exe = System.IO.Path.Combine(installDir, "pulseman.exe");
                Process.Start(new ProcessStartInfo(exe) { WorkingDirectory = installDir, UseShellExecute = true });
            } catch (Exception) { }
        }

        static void TryDelete(string path)
        {
            try {
                if (File.Exists(path))
                    File.Delete(path);
            } catch (Exception) { }
        }

        // ---------- testes ----------

        public void RunTest(string[] args)
        {
            if (args[0] == "/selftest") {
                // instala sem atalhos nem abrir o jogo e escreve o resultado em pasta\selftest.txt
                installDir = args[2];
                if (args.Length >= 4) SetLanguage(int.Parse(args[3]));   // idioma escolhido (teste)
                CheckRom(args[1]);
                Find<CheckBox>("OptDesktop").IsChecked = false;
                Find<CheckBox>("OptStart").IsChecked = false;
                Find<CheckBox>("OptLaunch").IsChecked = false;
                selfTest = true;
                Window.Loaded += (s, e) => {
                    if (romData == null) {
                        Directory.CreateDirectory(installDir);
                        File.WriteAllText(System.IO.Path.Combine(installDir, "selftest.txt"), "ROM recusada: " + romState);
                        Window.Close();
                        return;
                    }
                    StartInstall();
                };
                return;
            }
            string page = args[1], file = args[2];
            if (args.Length >= 5) SetLanguage(int.Parse(args[4]));
            Window.Loaded += (s, e) => {
                if (args.Length >= 4 && args[3] != "-") CheckRom(args[3]);
                if (page == "Info") {   // a janela de informações por cima da página da ROM
                    ShowPage("PageRom");
                    ShowInfo(true);
                    page = "PageRom";
                }
                ShowPage(page);
                if (page == "PageProgress") {
                    Find<TextBlock>("ProgressTitle").Text = T("installing");
                    Find<TextBlock>("ProgressStatus").Text = T("st_files");
                    SetProgress(0.62, null);
                }
                if (page == "PageDone") {
                    Find<TextBlock>("DoneTitle").Text = T("done_title");
                    Find<TextBlock>("DoneBody").Text = T("done_body");
                    ShowDone();
                }
                if (page == "PageError")
                    Find<TextBlock>("ErrorText").Text = T("err_running");
                var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1200) };
                t.Tick += (s2, e2) => {
                    t.Stop();
                    var root = (FrameworkElement)Window.Content;
                    var bmp = new RenderTargetBitmap((int)(root.ActualWidth + root.Margin.Left + root.Margin.Right),
                                                     (int)(root.ActualHeight + root.Margin.Top + root.Margin.Bottom), 96, 96, PixelFormats.Pbgra32);
                    bmp.Render(root);
                    var enc = new PngBitmapEncoder();
                    enc.Frames.Add(BitmapFrame.Create(bmp));
                    using (var f = File.Create(file))
                        enc.Save(f);
                    Window.Close();
                };
                t.Start();
            };
        }

        bool selfTest;

        // resultado em %TEMP%\pulseman-unselftest.txt (a pasta do jogo some)
        public void RunUninstallTest()
        {
            selfTest = true;
            Find<CheckBox>("OptKeepSaves").IsChecked = false;
            Window.Loaded += (s, e) => StartUninstall();
        }

        // ---------- desinstalação ----------

        void WireUninstallPage()
        {
            Find<Button>("UnCancel").Click += (s, e) => Window.Close();
            Find<Button>("UnConfirm").Click += (s, e) => StartUninstall();
        }

        void StartUninstall()
        {
            busy = true;
            Find<TextBlock>("ProgressTitle").Text = T("uninstalling");
            Find<TextBlock>("ProgressStatus").Text = "";
            ResetProgress();
            ShowPage("PageProgress");
            bool keep = Find<CheckBox>("OptKeepSaves").IsChecked == true;
            string dir = installDir;
            new Thread(() => {
                try {
                    if (GameRunning(dir)) {
                        Fail(T("err_running"));
                        return;
                    }
                    SetProgress(0.2, null);
                    foreach (string n in PayloadNames())
                        TryDelete(System.IO.Path.Combine(dir, n.Substring("payload/".Length).Replace('/', '\\')));
                    TryDelete(System.IO.Path.Combine(dir, RomName));
                    TryDelete(System.IO.Path.Combine(dir, "missing_entries.txt"));
                    TryDelete(System.IO.Path.Combine(dir, UninstallerName));
                    SetProgress(0.55, null);
                    if (!keep) {
                        TryDelete(System.IO.Path.Combine(dir, "pulseman.ini"));
                        for (int i = 1; i <= 3; i++)
                            TryDelete(System.IO.Path.Combine(dir, "pulseman-slot" + i + ".sav"));
                        // pasta de dados do usuário, usada quando a do jogo não aceita gravação
                        string pref = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Pulseman", "pulseman-recomp");
                        try { if (!selfTest && Directory.Exists(pref)) Directory.Delete(pref, true); } catch (Exception) { }
                    }
                    string img = System.IO.Path.Combine(dir, "img");
                    try { if (Directory.Exists(img) && !Directory.EnumerateFileSystemEntries(img).Any()) Directory.Delete(img); } catch (Exception) { }
                    try { if (Directory.Exists(dir) && !Directory.EnumerateFileSystemEntries(dir).Any()) Directory.Delete(dir); } catch (Exception) { }
                    if (!selfTest) {   // o teste não mexe nos atalhos de verdade
                        TryDelete(System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "Pulseman.lnk"));
                        TryDelete(System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Pulseman.lnk"));
                    }
                    SetProgress(0.85, null);
                    if (selfTest) {
                        File.WriteAllText(System.IO.Path.Combine(System.IO.Path.GetTempPath(), "pulseman-unselftest.txt"),
                                          Directory.Exists(dir) ? "sobrou: " + string.Join(", ", Directory.GetFileSystemEntries(dir)) : "ok");
                        Window.Dispatcher.Invoke(new Action(() => Window.Close()));
                        return;
                    }
                    Registry.CurrentUser.DeleteSubKeyTree(RegKey, false);
                    Thread.Sleep(300);
                    SetProgress(1.0, null);
                    Thread.Sleep(350);
                    Window.Dispatcher.Invoke(new Action(() => {
                        busy = false;
                        Find<TextBlock>("DoneTitle").Text = T("un_done_title");
                        Find<TextBlock>("DoneBody").Text = T("un_done_body");
                        Find<Button>("PlayButton").Visibility = Visibility.Collapsed;
                        ShowDone();
                        Window.Closed += (s2, e2) => DeleteSelfLater();
                    }));
                } catch (Exception ex) {
                    Fail(ex.Message);
                }
            }) { IsBackground = true }.Start();
        }

        // a cópia temporária do desinstalador se apaga depois de fechar
        static void DeleteSelfLater()
        {
            string self = Assembly.GetExecutingAssembly().Location;
            if (!self.StartsWith(System.IO.Path.GetTempPath(), StringComparison.OrdinalIgnoreCase))
                return;
            try {
                Process.Start(new ProcessStartInfo("cmd.exe", "/c timeout /t 2 /nobreak >nul & del /f /q \"" + self + "\"") {
                    CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden, UseShellExecute = false
                });
            } catch (Exception) { }
        }
    }
}
