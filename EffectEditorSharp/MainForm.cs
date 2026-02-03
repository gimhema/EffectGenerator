using System;
using System.Drawing;
using System.Windows.Forms;

namespace EffectEditor
{
    public sealed class MainForm : Form
    {
        private readonly SplitContainer _split;
        private readonly Panel _previewHost;

        private readonly TextBox _hlslEditor;
        private readonly Button _compileBtn;
        private readonly Label _statusLabel;

        private D3D11EffectPreviewer? _renderer;
        private readonly System.Windows.Forms.Timer _timer;

        public MainForm()
        {
            Text = "EffectEditor (D3D11 + HLSL)";
            ClientSize = new Size(1280, 720);
            KeyPreview = true;

            // ---- SplitContainer (상/하) ----
            _split = new SplitContainer
            {
                Dock = DockStyle.Fill,
                Orientation = Orientation.Horizontal,
                SplitterWidth = 6,
                SplitterDistance = (int)(ClientSize.Height * 0.60),
                Panel1MinSize = 200,
                Panel2MinSize = 150
            };
            Controls.Add(_split);

            // ---- 상단: 렌더 출력 영역 ----
            _previewHost = new Panel
            {
                Dock = DockStyle.Fill,
                BackColor = Color.Black
            };
            _split.Panel1.Controls.Add(_previewHost);

            // ---- 하단: 코드 에디터 + 버튼 + 상태 ----
            var bottomLayout = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 1,
                RowCount = 2
            };
            bottomLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            bottomLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40f));
            _split.Panel2.Controls.Add(bottomLayout);

            _hlslEditor = new TextBox
            {
                Dock = DockStyle.Fill,
                Multiline = true,
                AcceptsTab = true,
                ScrollBars = ScrollBars.Both,
                WordWrap = false,
                Font = new Font(FontFamily.GenericMonospace, 10f),
                Text = D3D11EffectPreviewer.DefaultHlsl
            };
            bottomLayout.Controls.Add(_hlslEditor, 0, 0);

            var bottomBar = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 3
            };
            bottomBar.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130f));
            bottomBar.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            bottomBar.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 10f));
            bottomLayout.Controls.Add(bottomBar, 0, 1);

            _compileBtn = new Button
            {
                Dock = DockStyle.Fill,
                Text = "Compile (F5)",
            };
            bottomBar.Controls.Add(_compileBtn, 0, 0);

            _statusLabel = new Label
            {
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleLeft,
                ForeColor = Color.DarkGreen,
                Text = "Ready"
            };
            bottomBar.Controls.Add(_statusLabel, 1, 0);

            // ---- 렌더 타이머 ----
            _timer = new System.Windows.Forms.Timer { Interval = 16 };
            _timer.Tick += (_, __) => _renderer?.Render();

            // ---- 이벤트 ----
            _previewHost.HandleCreated += (_, __) => CreateRendererIfNeeded();
            _previewHost.Resize += (_, __) =>
            {
                if (_renderer == null) return;
                if (_previewHost.ClientSize.Width <= 0 || _previewHost.ClientSize.Height <= 0) return;
                _renderer.Resize(_previewHost.ClientSize.Width, _previewHost.ClientSize.Height);
            };

            _compileBtn.Click += (_, __) => CompileFromEditor();

            KeyDown += (_, e) =>
            {
                if (e.KeyCode == Keys.F5)
                {
                    CompileFromEditor();
                    e.Handled = true;
                }
            };

            FormClosed += (_, __) =>
            {
                _timer.Stop();
                _renderer?.Dispose();
                _renderer = null;
            };
        }

        private void CreateRendererIfNeeded()
        {
            if (_renderer != null) return;
            if (_previewHost.ClientSize.Width <= 0 || _previewHost.ClientSize.Height <= 0) return;

            _renderer = new D3D11EffectPreviewer(
                _previewHost.Handle,
                _previewHost.ClientSize.Width,
                _previewHost.ClientSize.Height
            );

            // 첫 컴파일/반영
            _renderer.SetShaderSource(_hlslEditor.Text);
            TryCompileShowStatus();

            _timer.Start();
        }

        private void CompileFromEditor()
        {
            if (_renderer == null)
            {
                // 아직 Panel 핸들이 준비 안됐을 수도 있으니 생성 시도
                CreateRendererIfNeeded();
                if (_renderer == null) return;
            }

            _renderer.SetShaderSource(_hlslEditor.Text);
            TryCompileShowStatus();
        }

        private void TryCompileShowStatus()
        {
            try
            {
                _renderer!.RecompileShaders();
                _statusLabel.ForeColor = Color.DarkGreen;
                _statusLabel.Text = $"Compiled OK  ({DateTime.Now:HH:mm:ss})";
            }
            catch (Exception ex)
            {
                _statusLabel.ForeColor = Color.DarkRed;
                _statusLabel.Text = "Compile Failed: " + ex.Message;
            }
        }
    }
}
