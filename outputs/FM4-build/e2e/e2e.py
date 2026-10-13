"""End-to-end run of the FM4 build. Drives the diagnostic controller either from a fixed
timeline or toward a goal with Jev (TypeSafe System One) judging OCR'd screen text, then
collects GPU frame captures, the per-second perf trace and log error counts."""
import argparse, json, os, re, statistics, struct, subprocess, sys, time, zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BUTTONS = {'up': 0x1, 'down': 0x2, 'left': 0x4, 'right': 0x8, 'start': 0x10, 'back': 0x20,
           'lb': 0x100, 'rb': 0x200, 'a': 0x1000, 'b': 0x2000, 'x': 0x4000, 'y': 0x8000}

# Each step: [seconds to wait before, action, hold seconds]. Actions are button names joined
# with '+', or 'rt=<0..255>', 'lt=<0..255>', 'lx=<-32768..32767>', or 'none'.
SCENARIOS = {
    'boot': [[90, 'none', 0]],
    # Main menu -> Free Play -> Quick Race with the default Abarth 131 on Bernese Alps
    # Festival Circuit. The pre-race menu ignores A until its focus has moved once.
    'quickrace': [
        [35, 'a', 0.12], [12, 'right', 0.12], [1.2, 'right', 0.12], [1.2, 'right', 0.12],
        [2, 'a', 0.12], [6, 'a', 0.12], [8, 'a', 0.12], [5, 'a', 0.12], [6, 'a', 0.12],
        [6, 'a', 0.12], [6, 'a', 0.12], [5, 'a', 0.12], [40, 'a', 0.12],
        [5, 'down', 0.12], [1.5, 'up', 0.12], [1.5, 'a', 0.12], [8, 'rt=255', 0],
    ],
}

# Goals for Jev-driven navigation. The route is guidance for the judgment, not a script.
GOALS = {
    'quickrace': {
        'goal': 'Start a Quick Race from Free Play with the default car and the default track, '
                'then be driving in that race.',
        'route': ['Startup logos and videos: wait.', 'Title screen: start the game.',
                  'Main menu: open FREE PLAY.', 'Free Play menu: open QUICK RACE.',
                  'Manufacturer, car, paint color, location and track screens: accept the '
                  'highlighted default each time.',
                  'Assists screen: confirm the highlighted setting without changing anything.',
                  'Track screen: GO RACE.', 'Loading and pre-race camera: continue.',
                  'Pre-race menu: START RACE.'],
        # Screens this goal never needs; code backs out of them.
        'off_route': {'autovista', 'community_or_marketplace', 'career_menu', 'pause_menu'},
    },
}

SCREENS = {
    'logo_or_video': 'Startup logos, legal or audio logos, or an intro video',
    'title': 'Title screen asking to press A or START to start the game',
    'main_menu': 'Menu listing PLAY CAREER, COMMUNITY, AUTOVISTA, FREE PLAY and MARKETPLACE',
    'free_play_menu': 'Menu listing QUICK RACE, SPLIT SCREEN and HOT LAP',
    'career_menu': 'Career menus such as GO RACE, WORLD TOUR, EVENT LIST or season screens',
    'autovista': 'Autovista car showroom or its tutorial: move around the car, look around, '
                 'open the doors or hood, listen to the host',
    'community_or_marketplace': 'Community, storefront, marketplace, Xbox LIVE or online screens',
    'manufacturer_or_car_select': 'Choosing a manufacturer, a car model or a paint color',
    'assists_menu': 'Assists and difficulty list: braking, steering, traction control, shifting',
    'environment_or_track_select': 'Choosing a location or a track layout with laps and length',
    'loading': 'Loading screen showing the chosen car or track while the race loads',
    'pre_race_intro': 'Pre-race camera showing the car or track with no menu',
    'pre_race_menu': 'Pre-race menu listing START RACE, STARTING GRID, TUNE SETUP and QUIT RACE',
    'racing': 'Race in progress: HUD with place, lap counter and race timer, no menu',
    'pause_menu': 'Pause menu during a race, such as RESUME, RESTART and QUIT',
    'results': 'Race results or rewards',
    'dialog': 'Message box such as FEATURE UNAVAILABLE or a saving notice with OK',
    'other': None,
}
KEEP = 'the highlighted default'
NOTHING = 'nothing'
TEXTLESS_PROCEED = {'pre_race_intro', 'title'}
FOOTER_Y = 640


def write_png(path, width, height, rgb, scale):
    w, h = width // scale, height // scale
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        start = y * scale * width * 3
        line = rgb[start:start + width * 3]
        for x in range(w):
            rows += line[x * scale * 3:x * scale * 3 + 3]
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(rows), 6)) + chunk(b'IEND', b'')
    path.write_bytes(png)


def ppm_to_png(ppm, png, scale=2):
    data = ppm.read_bytes()
    parts = data.split(b'\n', 3)
    width, height = map(int, parts[1].split())
    write_png(png, width, height, parts[3], scale)


def set_input(path, buttons=0, lt=0, rt=0, lx=0, ly=0):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(f'{buttons} {lt} {rt} {lx} {ly} 0 0\n')
    os.replace(tmp, path)


def parse_action(action):
    state = dict(buttons=0, lt=0, rt=0, lx=0, ly=0)
    if action == 'none':
        return state
    for part in action.split('+'):
        if '=' in part:
            key, value = part.split('=')
            state[key] = int(value)
        else:
            state['buttons'] |= BUTTONS[part]
    return state


class Ocr:
    """Persistent Windows OCR process (ocr-server.ps1)."""
    def __init__(self):
        self.process = subprocess.Popen(
            ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
             str(HERE / 'ocr-server.ps1')], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, encoding='utf-8', errors='replace')
        if 'ready' not in self.process.stdout.readline():
            raise RuntimeError('OCR server failed to start')

    def read(self, ppm):
        self.process.stdin.write(f'{ppm}|2\n')
        self.process.stdin.flush()
        # OCR text may contain control characters such as tabs.
        result = json.loads(self.process.stdout.readline(), strict=False)
        if 'error' in result:
            raise RuntimeError('OCR: ' + result['error'])
        return result['lines']

    def close(self):
        self.process.stdin.close()
        self.process.wait(10)


def annotate(lines):
    """Marks the highlighted line: FM4 draws its selection in red near the item."""
    items = [l for l in lines if l['text'].strip()]
    # The red FORZA MOTORSPORT logo heads several menus; selected items may have red text.
    candidates = [l for l in items if 'FORZA' not in l['text'].upper() and l['y'] < FOOTER_Y]
    score = lambda l: max(l['red'], l['red_text'])
    ranked = sorted(candidates, key=lambda l: -score(l))
    if ranked and score(ranked[0]) >= 15 and (len(ranked) == 1 or score(ranked[0]) >= 1.3 * score(ranked[1])):
        ranked[0]['selected'] = True
    return sorted(items, key=lambda l: (l['y'] // 20, l['x']))


def newest_capture(capture, after=0.0):
    frames = [p for p in capture.glob('frame-*.ppm') if p.stat().st_mtime > after]
    frames += [p for p in capture.glob('live-latest.ppm') if p.stat().st_mtime > after]
    return max(frames, key=lambda p: p.stat().st_mtime) if frames else None


def jev_step(goal, lines, history):
    """One Jev request with independent judgments over the same screen state."""
    import jev
    texts = []
    for line in lines:
        if line['text'] not in texts:
            texts.append(line['text'])
    screen_text = [dict(text=l['text'], **({'highlighted': True} if l.get('selected') else {}),
                        **({'button_hint': True} if l['y'] >= FOOTER_Y else {})) for l in lines]
    state = {'goal': goal['goal'], 'route': goal['route'], 'screen_text': screen_text,
             'recent_steps': [{k: h[k] for k in ('screen', 'target', 'action')} for h in history[-6:]]}
    target_criteria = {text: None for text in texts[:250]}
    target_criteria[KEEP] = ('Keep the currently highlighted choice: the route says to accept the '
                             'default here, or the needed item is highlighted but its label is '
                             'not in `screen_text`')
    target_criteria[NOTHING] = ('Nothing to activate now: the screen is loading, a logo, video or '
                                'camera intro is playing, or no text is readable')
    questions = {
        'screen': {'type': 'choice', 'criteria': SCREENS,
                   'instructions': 'Which game screen is shown by `screen_text` (text read from the '
                                   'screen; empty when nothing is readable)? Use `recent_steps` for '
                                   'context about the previous screens.'},
        'target': {'type': 'choice', 'criteria': target_criteria,
                   'instructions': 'Following `route`, which item must the player activate on this '
                                   'screen to progress toward `goal`? Pick the own label of that item even '
                                   'when a different item is highlighted now. Entries marked '
                                   'button_hint are controller hints such as "A SELECT" or "GO RACE".'},
        'racing': {'type': 'noul',
                   'instructions': "Is the player's race in progress right now? The in-race HUD shows "
                                   "a lap counter such as '1/2', a race timer such as '00:28.686', a "
                                   "place such as '4/12' or the words PLACE and LAPS, with no menu list. "
                                   "Text is read by OCR and may be partial or garbled."},
    }
    answers = jev.system_one(state, questions)['answers']
    return answers


def choose_action(goal, answers, lines, history):
    """Code owns execution: turn the judgments into one controller action."""
    screen, target = answers['screen']['choice'], answers['target']['choice']
    signature = '|'.join(l['text'] for l in lines)
    same = 0
    for h in reversed(history):
        if h['signature'] != signature or h['action'] == 'wait':
            break
        same += 1
    if same >= 5:
        # Inputs no longer change this screen: assume a dead end off the route.
        return 'b', 'escape'
    if screen in goal['off_route']:
        return 'b', 'off route'
    last = history[-2:]
    if len(last) == 2 and all(h['action'] == 'a' and h['signature'] == signature for h in last):
        # Some FM4 menus ignore A until focus has moved once since they opened.
        return 'down,up,a', 'nudge'
    if screen == 'racing':
        return 'wait', 'racing'
    if target == NOTHING:
        if screen in TEXTLESS_PROCEED:
            return 'a', 'proceed'
        stalled = sum(1 for h in history[-4:] if h['action'] == 'wait' and h['signature'] == signature)
        return ('a', 'stalled') if stalled >= 3 and signature else ('wait', 'wait')
    if target == KEEP:
        return 'a', 'keep'
    line = next(l for l in lines if l['text'] == target)
    if line['y'] >= FOOTER_Y:
        return ('b' if re.search('BACK|CANCEL', target.upper()) else 'a'), 'hint'
    selected = next((l for l in lines if l.get('selected')), None)
    if not selected or selected is line:
        return 'a', 'activate'
    dx = (line['x'] + line['w'] / 2) - (selected['x'] + selected['w'] / 2)
    dy = (line['y'] + line['h'] / 2) - (selected['y'] + selected['h'] / 2)
    if abs(dx) > abs(dy):
        return ('right' if dx > 0 else 'left'), 'move'
    return ('down' if dy > 0 else 'up'), 'move'


def navigate(goal_name, process, input_file, capture, out, deadline, started):
    goal = GOALS[goal_name]
    ocr = Ocr()
    history, log = [], open(out / 'nav.jsonl', 'w', encoding='utf-8')
    seen_after = 0.0
    race_votes = 0
    try:
        while time.time() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'exited with {process.returncode}')
            frame = newest_capture(capture, seen_after)
            if not frame:
                time.sleep(0.2)
                continue
            lines = annotate(ocr.read(frame))
            answers = jev_step(goal, lines, history)
            screen, target = answers['screen'], answers['target']
            racing = answers['racing']['noul']
            # OCR reads only fragments of the race HUD, so also accept two consecutive
            # confident race-screen judgments backed by a moderate race probability.
            race_screen = screen['choice'] == 'racing' and screen['confidence'] >= 0.8 and racing >= 0.4
            race_votes = race_votes + 1 if race_screen else 0
            if racing >= 0.8 or race_votes >= 2:
                log.write(json.dumps({'t': round(time.time() - started, 1), 'screen': screen['choice'],
                                      'racing': racing, 'action': 'goal reached'}) + '\n')
                return True
            action, reason = choose_action(goal, answers, lines, history)
            pressed = time.time()
            for button in action.split(','):
                if button != 'wait':
                    set_input(input_file, **parse_action(button))
                    time.sleep(0.12)
                    set_input(input_file)
                    time.sleep(0.5)
            signature = '|'.join(l['text'] for l in lines)
            history.append({'screen': screen['choice'], 'target': target['choice'], 'action': action,
                            'signature': signature})
            log.write(json.dumps({
                't': round(pressed - started, 1), 'frame': frame.name,
                'screen': screen['choice'], 'screen_conf': screen['confidence'],
                'target': target['choice'], 'target_conf': target['confidence'],
                'selected': next((l['text'] for l in lines if l.get('selected')), None),
                'racing': round(racing, 3),
                'action': action, 'reason': reason,
                'text': [l['text'] for l in lines][:24]}) + '\n')
            log.flush()
            # Judge only frames captured after the input had time to take effect.
            seen_after = time.time() + 0.3
        return False
    finally:
        log.close()
        ocr.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-root', required=True, help='BuildRoot used by Build.ps1')
    parser.add_argument('--out', required=True, help='result directory, outside the repository')
    parser.add_argument('--scenario', default='boot')
    parser.add_argument('--goal', choices=sorted(GOALS), help='navigate with Jev instead of a timeline')
    parser.add_argument('--goal-timeout', type=float, default=360)
    parser.add_argument('--steps', help='JSON list of steps, overrides --scenario')
    parser.add_argument('--arg', action='append', default=[], help='extra/override --cvar=value')
    parser.add_argument('--tail', type=float, default=10, help='seconds to keep running after the last step')
    parser.add_argument('--keep-ppm', action='store_true', help='keep full-resolution captures')
    parser.add_argument('--measure', type=int, default=0, help='summarize only the last N perf seconds')
    options = parser.parse_args()

    root = Path(options.build_root)
    out = Path(options.out).resolve()
    # Captures and logs show game content; they must never land in the repository.
    if out == REPO or REPO in out.parents:
        parser.error('--out must be outside the repository, for example <BuildRoot>/e2e/<name>')
    out.mkdir(parents=True, exist_ok=True)
    capture = out / 'capture'
    capture.mkdir(exist_ok=True)
    for old in list(capture.glob('*')) + [out / 'perf.jsonl', out / 'runtime.log', out / 'nav.jsonl']:
        if old.is_file():
            old.unlink()
    state = json.loads((root / 'local.json').read_text())
    disc1 = state['disc1']
    input_file = out / 'input.txt'
    set_input(input_file)

    # Mirrors pipeline.py launch(); keep both in sync.
    args = {
        'game_data_root': disc1, 'user_data_root': str(root / 'user-data'),
        'cache_root': str(root / 'cache'), 'allow_game_relative_writes': 'false',
        'headless': 'false', 'fullscreen': 'false', 'render_target_path_d3d12': 'rov',
        'readback_resolve': 'full', 'gpu_allow_invalid_fetch_constants': 'true',
        'async_shader_compilation': 'false', 'rov_occlusion_awaited_report_divisor': '2',
        'input_backend': 'sdl', 'mnk_mode': 'true', 'mnk_mouse': 'false',
        'log_file': str(out / 'runtime.log'),
    }
    for item in options.arg:
        key, value = item.lstrip('-').split('=', 1)
        args[key] = value
    command = [str(root / 'game-build' / 'fm4_rex.exe')] + [f'--{k}={v}' for k, v in args.items()]
    env = {k: v for k, v in os.environ.items() if not k.startswith(('FM4_', 'REX_'))}
    env.update(FM4_SHADOW_WORKAROUND='1', FM4_CAPTURE_DIR=str(capture),
               FM4_TEST_INPUT_FILE=str(input_file), REX_PERF_TRACE_FILE=str(out / 'perf.jsonl'))
    steps = [] if options.goal else (json.loads(options.steps) if options.steps else SCENARIOS[options.scenario])

    stderr = open(out / 'stderr.log', 'w', encoding='utf-8', errors='replace')
    started = time.time()
    process = subprocess.Popen(command, cwd=root, env=env, stdout=stderr, stderr=subprocess.STDOUT)
    timeline = []
    crashed = None
    goal_reached = None
    try:
        if options.goal:
            goal_reached = navigate(options.goal, process, input_file, capture, out,
                                    started + options.goal_timeout, started)
            if goal_reached:
                set_input(input_file, rt=255)
        for wait, action, hold in steps:
            deadline = time.time() + wait
            while time.time() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f'exited with {process.returncode}')
                time.sleep(0.1)
            set_input(input_file, **parse_action(action))
            timeline.append([round(time.time() - started, 1), action])
            if hold:
                time.sleep(hold)
                set_input(input_file)
        deadline = time.time() + options.tail
        while time.time() < deadline and process.poll() is None:
            time.sleep(0.1)
        if process.poll() is not None:
            raise RuntimeError(f'exited with {process.returncode}')
    except RuntimeError as error:
        crashed = str(error)
    except Exception as error:
        crashed = f'harness error: {error!r}'
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        stderr.close()

    frames = sorted(capture.glob('frame-*.ppm'))
    if frames:
        # Keep the final capture under a stable name for checkpoints.
        ppm_to_png(frames[-1], out / 'final.png')
    for frame in frames:
        ppm_to_png(frame, frame.with_suffix('.png'))
        if not options.keep_ppm:
            frame.unlink()

    perf = [json.loads(line) for line in (out / 'perf.jsonl').read_text().splitlines()] \
        if (out / 'perf.jsonl').is_file() else []
    steady = perf[-options.measure:] if options.measure else (perf[len(perf) // 3:] if len(perf) > 6 else perf)
    log_text = (out / 'runtime.log').read_text(encoding='utf-8', errors='replace') \
        if (out / 'runtime.log').is_file() else ''
    errors = {}
    for line in log_text.splitlines():
        if re.search(r'\b(E|error|ERROR)\b|\[error\]|\[E\]', line):
            key = re.sub(r'^\S+\s+\S+\s+', '', line)[:90]
            key = re.sub(r'0x[0-9A-Fa-f]+|\d+', '#', key)
            errors[key] = errors.get(key, 0) + 1
    summary = {
        'crashed': crashed, 'goal': options.goal, 'goal_reached': goal_reached,
        'seconds': round(time.time() - started, 1), 'timeline': timeline,
        'frames_captured': len(frames), 'perf_windows': len(perf),
        'fps_median': round(statistics.median([p['fps'] for p in steady]), 1) if steady else None,
        'fps_min': round(min(p['fps'] for p in steady), 1) if steady else None,
        'wait_ms_per_s_median': {k: round(statistics.median([p['wait_ms'][k] / p['window_ms'] * 1000 for p in steady]), 1)
                                 for k in steady[0]['wait_ms']} if steady else {},
        'readbacks_per_s_median': round(statistics.median([p['readbacks'] for p in steady]), 1) if steady else None,
        'draws_per_s_median': round(statistics.median([p['draws'] for p in steady]), 1) if steady else None,
        'top_errors': sorted(errors.items(), key=lambda kv: -kv[1])[:10],
    }
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
    if crashed or goal_reached is False:
        sys.exit(1)


if __name__ == '__main__':
    main()
