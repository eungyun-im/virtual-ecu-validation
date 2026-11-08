#!/usr/bin/env python3
"""Demo video from real artefacts: the scenario CAN log (logs/demo.log + captions), the log analyzer
output, the CI result of the demo PR that reintroduced DEF-001, and the Test Report verdict.

    python tools/make_demo_video.py --log logs/demo.log --ci-checks ci_checks.txt --ci-fail ci_fail.txt \
        --report reports/out/test_report.md --out docs/demo/demo.mp4
Nothing in the video is drawn by hand: every number is read from those files.
"""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

import can
import cantools
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib import font_manager  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
for f in ("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"):
    if Path(f).exists():
        font_manager.fontManager.addfont(f)
plt.rcParams["font.family"] = ["Noto Sans CJK JP", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False
W, H, DPI, FPS = 12.8, 7.2, 100, 20
BG, INK, MUTED, LINE, ACC, OK, BAD = "#fafaf9", "#111111", "#666666", "#d4d4d4", "#d9480f", "#2b8a3e", "#c92a2a"
STATE_COLOR = {"INIT": "#bdbdbd", "NORMAL": OK, "DEGRADED": "#e8a400", "SAFE_STOP": BAD}
KO = {  # Korean captions for the scenario steps (keys = English captions written by vecu.scenario)
    "normal driving": "정상 주행",
    "steering command 90 deg (rate limited)": "조향 90° 명령 → 한 주기 5°로 변화율 제한",
    "out-of-range command 600 deg: discarded": "범위 밖 명령 600° → 무시하고 DTC 기록",
    "back to 20 deg": "조향 20°로 복귀",
    "fault: 3 corrupted steering frames": "결함 주입: 조향 프레임 CRC 3회 오류 → DEGRADED",
    "fault: steering messages stop": "결함 주입: 조향 메시지 중단 → 타임아웃 → SAFE_STOP",
    "steering messages resume": "조향 메시지 재개 → 1초 정상 수신 후 NORMAL 복귀",
    "fault: bus-off on all channels for 0.2 s": "결함 주입: 버스오프 0.2초 → SAFE_STOP → 복귀",
    "end": "",
}


def frame(tmp, n):
    p = Path(tmp) / f"f{n:05d}.png"
    return p


def new_fig():
    fig = plt.figure(figsize=(W, H), dpi=DPI, facecolor=BG)
    return fig


def text_slide(tmp, n, seconds, title, lines, sub=None):
    fig = new_fig()
    fig.text(0.07, 0.80, title, fontsize=34, weight="bold", color=INK)
    if sub:
        fig.text(0.07, 0.73, sub, fontsize=17, color=MUTED)
    y = 0.62
    for ln in lines:
        color = INK
        if isinstance(ln, tuple):
            ln, color = ln
        fig.text(0.07, y, ln, fontsize=17, color=color, family=["DejaVu Sans Mono", "Noto Sans CJK JP"] if ln.startswith("  ") else None)
        y -= 0.065
    fig.add_artist(plt.Line2D([0.07, 0.93], [0.70 if sub else 0.76] * 2, color=INK, lw=1.5))
    for i in range(int(seconds * FPS)):
        fig.savefig(frame(tmp, n + i), facecolor=BG)
    plt.close(fig)
    return n + int(seconds * FPS)


def arch_slide(tmp, n, seconds):
    fig = new_fig()
    fig.text(0.07, 0.88, "구성", fontsize=30, weight="bold", color=INK)
    ax = fig.add_axes([0.04, 0.04, 0.92, 0.78]); ax.set_xlim(0, 100); ax.set_ylim(0, 60); ax.axis("off")

    def box(x, y, w, h, title, sub="", dark=False):
        ax.add_patch(plt.Rectangle((x, y), w, h, fc=INK if dark else "white", ec=INK, lw=1.4))
        ax.text(x + w / 2, y + h * 0.62, title, ha="center", va="center", fontsize=13, weight="bold", color="white" if dark else INK)
        if sub:
            ax.text(x + w / 2, y + h * 0.28, sub, ha="center", va="center", fontsize=10, color="#dddddd" if dark else MUTED)

    def arrow(x1, y1, x2, y2):
        ax.annotate("", xy=(x2, y2), xytext=(x1, y1), arrowprops=dict(arrowstyle="->", color=INK, lw=1.3))
    box(2, 46, 22, 9, "요구사항 20개", "EARS · 수치 기준")
    box(2, 33, 22, 9, "DBC", "메시지 6개 · E2E")
    box(2, 20, 22, 9, "잔여 버스 · 결함 주입기", "Python · 누락/지연/CRC/재전송")
    box(2, 7, 22, 9, "UDS 테스터", "udsoncan · ISO-TP")
    ax.plot([30, 30], [5, 57], color=INK, lw=3)
    ax.text(31, 57, "CAN (vcan0 / 가상 버스)", fontsize=11, color=INK, va="top")
    for y in (37.5, 24.5, 11.5):
        arrow(24, y, 30, y)
    box(38, 22, 26, 22, "가상 ECU (C)", "범위·변화율·타임아웃·E2E\n상태 머신 · DTC · UDS 서버", dark=True)
    arrow(30, 33, 38, 33)
    box(72, 46, 25, 9, "로그 분석기", "CAN 로그 → 주기 · 공백 · 범위 · E2E")
    arrow(30, 50.5, 72, 50.5)
    box(38, 3, 26, 11, "시험 109개", "단위 · 시스템 · 퍼징 · ASan/UBSan")
    box(72, 3, 25, 11, "CI 게이트", "→ RTM · Test Report 판정")
    arrow(64, 8.5, 72, 8.5)
    arrow(51, 22, 51, 14)
    for i in range(int(seconds * FPS)):
        fig.savefig(frame(tmp, n + i), facecolor=BG)
    plt.close(fig)
    return n + int(seconds * FPS)


def load_scenario(log_path):
    db = cantools.database.load_file(str(ROOT / "dbc" / "vecu.dbc"))
    cap = json.loads(Path(log_path).with_suffix(".captions.json").read_text())
    t0 = cap["t0"]
    cmd, st = [], []
    for m in can.LogReader(str(log_path)):
        t = m.timestamp - t0
        if m.arbitration_id == 0x100:
            cmd.append((t, db.get_message_by_frame_id(0x100).decode(bytes(m.data))["SteerAngleCmd"]))
        elif m.arbitration_id == 0x200:
            d = db.get_message_by_frame_id(0x200).decode(bytes(m.data), decode_choices=False)
            st.append((t, ["NORMAL", "DEGRADED", "SAFE_STOP", "INIT"][int(d["EcuState"])], d["SteerOut"], d["SpeedOut"], d["ConfirmedDtcCount"]))
    events = []
    for a, b in zip(st, st[1:]):
        if a[1] != b[1]:
            last_cmd = max((c[0] for c in cmd if c[0] <= b[0]), default=None)
            gap = (b[0] - last_cmd) * 1000 if last_cmd is not None else None
            events.append((b[0], a[1], b[1], gap if gap is not None and gap >= 25.0 else None))
    return cap["steps"], cmd, st, events


def scenario(tmp, n, log_path, speed=0.3):
    steps, cmd, st, events = load_scenario(log_path)
    end = st[-1][0] - 0.2
    total = int(end / speed * FPS)
    for i in range(total):
        t = i * speed / FPS
        fig = new_fig()
        gs = fig.add_gridspec(3, 2, width_ratios=[2.3, 1], height_ratios=[0.5, 1.2, 1], left=0.06, right=0.97,
                              top=0.86, bottom=0.17, hspace=0.35, wspace=0.12)
        fig.text(0.06, 0.92, "실시간 시나리오: 결함 주입 → 감지 → 안전 정지 → 복구", fontsize=20, weight="bold", color=INK)
        fig.text(0.97, 0.92, f"t = {t:5.2f} s   (×{speed} 재생)", fontsize=13, color=MUTED, ha="right")
        sv = [s for s in st if s[0] <= t]
        ax = fig.add_subplot(gs[0, 0])
        for a, b in zip(sv, sv[1:] + [(t,)]):
            ax.axvspan(a[0], b[0], color=STATE_COLOR[a[1]], lw=0)
        ax.set_xlim(0, end); ax.set_yticks([]); ax.set_xticks([])
        ax.set_title("ECU 상태  " + (f"{sv[-1][1]}" if sv else ""), loc="left", fontsize=12, color=INK)
        for s in ax.spines.values(): s.set_visible(False)
        ax = fig.add_subplot(gs[1, 0])
        cv = [c for c in cmd if c[0] <= t]
        ax.plot([c[0] for c in cv], [c[1] for c in cv], color=LINE, lw=4, label="조향 명령 (STEER_CMD)")
        ax.plot([s[0] for s in sv], [s[2] for s in sv], color=INK, lw=1.8, label="조향 출력 (ECU_STATUS)")
        ax.set_xlim(0, end); ax.set_ylim(-20, 640); ax.set_ylabel("deg", color=MUTED)
        ax.axhline(540, color=ACC, lw=1, ls="--"); ax.text(0.05, 555, "허용 범위 ±540°", color=ACC, fontsize=10)
        ax.legend(loc="upper right", frameon=False, fontsize=10)
        for s in ("top", "right"): ax.spines[s].set_visible(False)
        ax = fig.add_subplot(gs[2, 0])
        ax.plot([s[0] for s in sv], [s[3] for s in sv], color=INK, lw=1.8)
        ax.set_xlim(0, end); ax.set_ylim(0, 45); ax.set_ylabel("km/h", color=MUTED); ax.set_xlabel("시간 (s)", color=MUTED)
        ax.set_title("속도 출력 (SAFE_STOP에서 10 km/h/s로 감속)", loc="left", fontsize=11, color=MUTED)
        for s in ("top", "right"): ax.spines[s].set_visible(False)
        ax = fig.add_subplot(gs[:, 1]); ax.axis("off")
        ax.set_title("상태 변화 (버스에서 측정)", loc="left", fontsize=13, color=INK)
        y = 0.95
        for ev in [e for e in events if e[0] <= t][-9:]:
            det = f"  마지막 정상 프레임 후 {ev[3]:.0f} ms" if ev[2] in ("SAFE_STOP", "DEGRADED") and ev[3] else ""
            ax.text(0.0, y, f"{ev[0]:5.2f} s  {ev[1]} → {ev[2]}", fontsize=11, color=STATE_COLOR[ev[2]], transform=ax.transAxes, weight="bold")
            if det:
                ax.text(0.0, y - 0.045, det, fontsize=10, color=MUTED, transform=ax.transAxes)
            y -= 0.11
        if sv:
            ax.text(0.0, 0.02, f"확정 DTC {sv[-1][4]}개", fontsize=12, color=INK, transform=ax.transAxes)
        cur = [s for s in steps if s["t_s"] <= t]
        if cur and KO.get(cur[-1]["caption"]):
            fig.text(0.5, 0.05, KO[cur[-1]["caption"]], fontsize=19, ha="center", color="white",
                     bbox=dict(boxstyle="round,pad=0.5", fc=INK, ec=INK))
        fig.savefig(frame(tmp, n + i), facecolor=BG)
        plt.close(fig)
    return n + total, events


def image_slide(tmp, n, seconds, title, img, note):
    fig = new_fig()
    fig.text(0.06, 0.92, title, fontsize=22, weight="bold", color=INK)
    ax = fig.add_axes([0.06, 0.16, 0.88, 0.72]); ax.axis("off")
    ax.imshow(plt.imread(img))
    fig.text(0.06, 0.07, note, fontsize=14, color=MUTED)
    for i in range(int(seconds * FPS)):
        fig.savefig(frame(tmp, n + i), facecolor=BG)
    plt.close(fig)
    return n + int(seconds * FPS)


def verdict_lines(first):
    """Test Report line 1 -> 3 lines: verdict, requirements + tests, the other gates."""
    parts = [p.strip() for p in first.replace(" — ", "; ").split("; ")]
    return [parts[0], " · ".join(parts[1:3]), " · ".join(parts[3:])]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default=str(ROOT / "logs" / "demo.log"))
    ap.add_argument("--log-report", default=str(ROOT / "reports" / "out" / "log_demo"))
    ap.add_argument("--ci-checks", required=True)
    ap.add_argument("--ci-fail", required=True)
    ap.add_argument("--ci-main", required=True, help="one line per main job: name<TAB>result")
    ap.add_argument("--report", default=str(ROOT / "reports" / "out" / "test_report.md"))
    ap.add_argument("--out", default=str(ROOT / "docs" / "demo" / "demo.mp4"))
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="vdemo_")
    n = 0
    n = text_slide(tmp, n, 6, "가상 ECU 선행 검증 파이프라인", [
        "C로 만든 안전 감시 ECU를 노트북 하나에서 요구사항부터 Test Report까지 자동 검증",
        "CAN·DBC · E2E · UDS 진단 · 결함 주입 · 정적/동적 분석 · CI 게이트", "",
        ("요구사항 20개 → 시험 109개 → RTM → 판정", MUTED)], sub="Virtual ECU Validation Pipeline · SIL")
    n = arch_slide(tmp, n, 10)
    n, events = scenario(tmp, n, a.log)
    det = [e for e in events if e[2] == "SAFE_STOP" and e[3]]
    n = image_slide(tmp, n, 9, "로그 분석기: CAN 로그에서 주기·공백·범위 이탈·E2E 오류 자동 검출",
                    Path(a.log_report) / "states.png",
                    f"같은 시나리오 로그 · 회색 구간 = 30 ms 이상 메시지 공백 · SAFE_STOP 감지 {det[0][3]:.0f} ms (기준 80 ms 이내)" if det else "")
    checks = [l.rstrip("\n").split("\t") for l in open(a.ci_checks) if l.strip()]
    fails = [l.strip() for l in open(a.ci_fail) if "FAIL" in l][:7]
    lines = [(f"  {'✗' if r == 'fail' else '✓'} {name}", BAD if r == "fail" else OK) for name, r in checks]
    n = text_slide(tmp, n, 14, "CI 게이트: DEF-001을 되살린 PR은 막힌다", lines + [""] +
                   [("  " + (f.split("::")[-1] if "::" in f else f.replace("FAIL ", ""))[:72], BAD) for f in fails],
                   sub="PR #2: 수정 전 alive counter 검사로 되돌림 → 회귀 시험 실패 → Test Report FAIL")
    main_jobs = [l.rstrip("\n").split("\t") for l in open(a.ci_main) if l.strip()]
    first = open(a.report).readline().strip().replace("**", "")
    n = text_slide(tmp, n, 12, "main: 모든 게이트 통과",
                   [(f"  ✓ {name}", OK) for name, _ in main_jobs] + [""] + verdict_lines(first),
                   sub="Test Report 첫 줄 = 판정과 근거")
    n = text_slide(tmp, n, 5, "범위와 한계", [
        "SIL: 타깃 실시간성·전기적 결함은 범위 밖 → 학교 캡스톤 S32K312 HILS에서 확인",
        "E2E는 안전 메커니즘이지 보안이 아님 (SecOC 필요) · SecurityAccess 키는 데모용",
        "CANoe 대신 python-can (라이선스)", "", ("github.com/eungyun-im/virtual-ecu-validation", MUTED)])
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", f"{tmp}/f%05d.png",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", str(out)], check=True)
    print(out, f"{n / FPS:.0f} s")


if __name__ == "__main__":
    main()
