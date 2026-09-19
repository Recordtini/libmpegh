import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import wave
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

APP_TITLE = "MPEG-H Raw Object Explorer"


def app_dir():
    target = sys.executable if getattr(sys, "frozen", False) else __file__
    return os.path.dirname(os.path.abspath(target))


def backend_path():
    for name in ("mpegh_backend.exe", "mpegh_raw_extract.exe", "ia_mpeghd_testbench.exe"):
        path = os.path.join(app_dir(), name)
        if os.path.exists(path):
            return path
    return os.path.join(app_dir(), "mpegh_backend.exe")


def wav_duration(path):
    try:
        with wave.open(path, "rb") as wav:
            rate = wav.getframerate()
            return wav.getnframes() / float(rate) if rate else 0.0
    except Exception:
        return 0.0


def fmt_time(seconds):
    if seconds <= 0:
        return "—"
    minutes = int(seconds // 60)
    seconds -= minutes * 60
    return f"{minutes}:{seconds:05.2f}"


class SceneExplorer(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1040x680")
        self.minsize(860, 560)

        self.temp_root = None
        self.materials_dir = None
        self.source_path = None
        self.rows = {}
        self.manifest = {}

        self.build_ui()
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
            self.after(150, lambda: self.analyze_file(sys.argv[1]))

    def build_ui(self):
        top = ttk.Frame(self, padding=12)
        top.pack(fill="x")
        ttk.Button(top, text="Open M4A / MP4…", command=self.open_file).pack(side="left")
        self.file_label = ttk.Label(top, text="Drop a file on this EXE or choose Open.")
        self.file_label.pack(side="left", padx=12, fill="x", expand=True)

        scene = ttk.LabelFrame(self, text="Raw decoded MPEG-H sources", padding=10)
        scene.pack(fill="x", padx=12, pady=(0, 8))
        self.summary_label = ttk.Label(scene, text="No file loaded.")
        self.summary_label.pack(anchor="w")
        ttk.Label(
            scene,
            text="Raw Object Sources are decoded before OAM positioning/rendering. Spatial metadata does not alter these WAVs.",
            foreground="#666666",
        ).pack(anchor="w", pady=(4, 0))

        controls = ttk.Frame(self, padding=(12, 0))
        controls.pack(fill="x")
        ttk.Button(controls, text="Select raw objects", command=self.select_objects).pack(side="left")
        ttk.Button(controls, text="Select all", command=lambda: self.set_all(True)).pack(side="left", padx=(6, 0))
        ttk.Button(controls, text="Select none", command=lambda: self.set_all(False)).pack(side="left", padx=(6, 0))
        ttk.Button(controls, text="Preview selected", command=self.preview_selected).pack(side="left", padx=(18, 0))
        self.export_btn = ttk.Button(controls, text="Export selected…", command=self.export_selected, state="disabled")
        self.export_btn.pack(side="right")

        columns = ("pick", "type", "name", "duration", "details")
        self.tree = ttk.Treeview(self, columns=columns, show="headings", selectmode="browse")
        self.tree.heading("pick", text="Export")
        self.tree.heading("type", text="Type")
        self.tree.heading("name", text="Signal")
        self.tree.heading("duration", text="Length")
        self.tree.heading("details", text="Source / notes")
        self.tree.column("pick", width=60, anchor="center", stretch=False)
        self.tree.column("type", width=120, stretch=False)
        self.tree.column("name", width=150, stretch=False)
        self.tree.column("duration", width=90, anchor="center", stretch=False)
        self.tree.column("details", width=520)
        self.tree.pack(fill="both", expand=True, padx=12, pady=8)
        self.tree.bind("<Button-1>", self.on_tree_click)
        self.tree.bind("<Double-1>", lambda _event: self.preview_selected())

        bottom = ttk.Frame(self, padding=(12, 0, 12, 12))
        bottom.pack(fill="x")
        self.status = ttk.Label(bottom, text="Ready.")
        self.status.pack(side="left")
        self.progress = ttk.Progressbar(bottom, mode="indeterminate", length=180)
        self.progress.pack(side="right")

    def open_file(self):
        path = filedialog.askopenfilename(
            title="Open MPEG-H file",
            filetypes=[("MPEG-4 audio/video", "*.m4a *.mp4 *.mhm1"), ("All files", "*.*")]
        )
        if path:
            self.analyze_file(path)

    def clear_scene(self):
        for item in self.tree.get_children():
            self.tree.delete(item)
        self.rows.clear()
        self.export_btn.config(state="disabled")

    def analyze_file(self, path):
        self.clear_scene()
        self.source_path = os.path.abspath(path)
        self.file_label.config(text=self.source_path)
        self.summary_label.config(text="Analyzing and decoding transport signals…")
        self.status.config(text="Decoding once into a temporary workspace…")
        self.progress.start(10)

        if self.temp_root:
            shutil.rmtree(self.temp_root, ignore_errors=True)
        self.temp_root = tempfile.mkdtemp(prefix="mpegh_scene_")
        self.materials_dir = os.path.join(self.temp_root, "materials")
        rendered_mix = os.path.join(self.temp_root, "rendered_mix.wav")

        threading.Thread(
            target=self.analyze_worker,
            args=(self.source_path, rendered_mix, self.materials_dir),
            daemon=True
        ).start()

    def analyze_worker(self, source, rendered_mix, materials):
        backend = backend_path()
        cmd = [
            backend,
            f"-ifile:{source}",
            f"-ofile:{rendered_mix}",
            "-pcmsz:24",
            "-raw_materials:1",
            f"-rawdir:{materials}",
        ]
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        try:
            proc = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
                creationflags=flags,
            )
            if proc.returncode != 0:
                self.after(0, lambda: self.analysis_failed(proc.stdout, proc.returncode))
                return
            manifest_path = os.path.join(materials, "manifest.json")
            with open(manifest_path, "r", encoding="utf-8") as handle:
                manifest = json.load(handle)
            self.after(0, lambda: self.populate(manifest, rendered_mix))
        except Exception as exc:
            self.after(0, lambda: self.analysis_failed(str(exc), -1))

    def analysis_failed(self, output, code):
        self.progress.stop()
        self.summary_label.config(text="Could not analyze this file.")
        self.status.config(text=f"Decoder failed ({code}).")
        messagebox.showerror(APP_TITLE, f"Decoder failed.\n\n{output[-5000:]}")

    def add_row(self, kind, name, path, details="", checked=False):
        duration = wav_duration(path)
        iid = self.tree.insert(
            "",
            "end",
            values=("[x]" if checked else "[ ]", kind, name, fmt_time(duration), details),
        )
        self.rows[iid] = {
            "checked": checked,
            "kind": kind,
            "name": name,
            "path": path,
        }
        return duration

    def populate(self, manifest, rendered_mix):
        self.progress.stop()
        self.manifest = manifest
        transport = manifest.get("transport", {})
        object_details = {
            int(item.get("index", -1)): item
            for item in manifest.get("object_details", [])
        }

        longest = 0.0

        for index in range(int(transport.get("objects", 0))):
            path = os.path.join(self.materials_dir, "objects", f"object_{index:02d}.wav")
            meta = object_details.get(index, {})
            details = (
                f"pre-OAM decoded source; transport signal "
                f"{meta.get('transport_index', index)}; no spatial rendering applied"
            )
            longest = max(
                longest,
                self.add_row("Raw object", f"Object {index:02d}", path, details, True),
            )

        for index in range(int(transport.get("channel_signals", 0))):
            path = os.path.join(self.materials_dir, "channels", f"channel_{index:02d}.wav")
            longest = max(
                longest,
                self.add_row(
                    "Channel bed",
                    f"Channel {index:02d}",
                    path,
                    "decoded channel signal before rendering/downmix",
                    False,
                ),
            )

        for index in range(int(transport.get("hoa_transport_channels", 0))):
            path = os.path.join(
                self.materials_dir, "hoa_transport", f"hoa_transport_{index:02d}.wav"
            )
            longest = max(
                longest,
                self.add_row(
                    "HOA transport",
                    f"HOA {index:02d}",
                    path,
                    "HOA transport coefficient/channel",
                    False,
                ),
            )

        for entry in manifest.get("rendered_channels", []):
            index = int(entry.get("index", 0))
            path = os.path.join(self.materials_dir, "rendered", f"speaker_{index:02d}.wav")
            details = f"az {entry.get('azimuth', 0)}°, el {entry.get('elevation', 0)}°"
            if entry.get("lfe"):
                details += "; LFE"
            longest = max(
                longest,
                self.add_row(
                    "Rendered",
                    f"Speaker {index:02d}",
                    path,
                    details,
                    False,
                ),
            )

        if os.path.exists(rendered_mix):
            longest = max(
                longest,
                self.add_row(
                    "Rendered mix",
                    "Rendered mix",
                    rendered_mix,
                    "normal multichannel decoder output",
                    False,
                ),
            )

        sample_rate = int(manifest.get("transport_sample_rate", 0))
        self.summary_label.config(
            text=(
                f"{transport.get('objects', 0)} objects • "
                f"{transport.get('channel_signals', 0)} channel-bed signals • "
                f"{transport.get('hoa_transport_channels', 0)} HOA transport signals • "
                f"{sample_rate / 1000.0:.1f} kHz • {fmt_time(longest)}"
            )
        )
        self.status.config(
            text="Analysis complete. Raw pre-OAM object sources are selected by default."
        )
        self.export_btn.config(state="normal")

    def on_tree_click(self, event):
        region = self.tree.identify("region", event.x, event.y)
        column = self.tree.identify_column(event.x)
        iid = self.tree.identify_row(event.y)
        if region == "cell" and column == "#1" and iid in self.rows:
            row = self.rows[iid]
            row["checked"] = not row["checked"]
            values = list(self.tree.item(iid, "values"))
            values[0] = "[x]" if row["checked"] else "[ ]"
            self.tree.item(iid, values=values)
            return "break"

    def set_all(self, value):
        for iid, row in self.rows.items():
            row["checked"] = value
            values = list(self.tree.item(iid, "values"))
            values[0] = "[x]" if value else "[ ]"
            self.tree.item(iid, values=values)

    def select_objects(self):
        for iid, row in self.rows.items():
            checked = row["kind"] == "Raw object"
            row["checked"] = checked
            values = list(self.tree.item(iid, "values"))
            values[0] = "[x]" if checked else "[ ]"
            self.tree.item(iid, values=values)

    def preview_selected(self):
        selected = self.tree.selection()
        if not selected:
            messagebox.showinfo(APP_TITLE, "Select a row first.")
            return
        row = self.rows.get(selected[0])
        if row and os.path.exists(row["path"]):
            os.startfile(row["path"])

    def export_selected(self):
        chosen = [
            row
            for row in self.rows.values()
            if row["checked"] and os.path.exists(row["path"])
        ]
        if not chosen:
            messagebox.showinfo(APP_TITLE, "Nothing is checked for export.")
            return

        parent = filedialog.askdirectory(title="Choose export folder")
        if not parent:
            return

        base = os.path.splitext(os.path.basename(self.source_path))[0] + "_export"
        destination = os.path.join(parent, base)
        os.makedirs(destination, exist_ok=True)

        kind_dirs = {
            "Raw object": "objects",
            "Channel bed": "channels",
            "HOA transport": "hoa_transport",
            "Rendered": "rendered",
            "Rendered mix": "",
        }

        for row in chosen:
            subdir = kind_dirs.get(row["kind"], "")
            target_dir = os.path.join(destination, subdir) if subdir else destination
            os.makedirs(target_dir, exist_ok=True)
            shutil.copy2(
                row["path"],
                os.path.join(target_dir, os.path.basename(row["path"])),
            )

        manifest_src = os.path.join(self.materials_dir, "manifest.json")
        if os.path.exists(manifest_src):
            shutil.copy2(manifest_src, os.path.join(destination, "manifest.json"))

        self.status.config(text=f"Exported {len(chosen)} files to {destination}")
        messagebox.showinfo(APP_TITLE, f"Export complete.\n\n{destination}")

    def on_close(self):
        if self.temp_root:
            shutil.rmtree(self.temp_root, ignore_errors=True)
        self.destroy()


if __name__ == "__main__":
    SceneExplorer().mainloop()
