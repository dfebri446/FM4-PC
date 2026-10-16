"""End-to-end run of the FM4 build. Drives the diagnostic controller either from a fixed
timeline or toward a goal with Jev (TypeSafe System One) judging OCR'd screen text, then
collects GPU frame captures, the per-second perf trace and log error counts."""
import argparse, json, os, re, shutil, statistics, struct, subprocess, sys, time, zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BUTTONS = {'up': 0x1, 'down': 0x2, 'left': 0x4, 'right': 0x8, 'start': 0x10, 'back': 0x20,
           'lb': 0x100, 'rb': 0x200, 'a': 0x1000, 'b': 0x2000, 'x': 0x4000, 'y': 0x8000}

# Each step: [seconds to wait before, action, hold seconds]. Actions are button names joined
# with '+', or 'rt=<0..255>', 'lt=<0..255>', 'lx/ly/rx/ry=<-32768..32767>', or 'none'; a
# comma-separated sequence taps each in turn (released between). These change no input:
#   'wait'             only waits (keeps a held throttle);
#   'phase:<name>'     starts a measured phase ('phase:end' closes it) for summary.json phases;
#   'select:<label>'   moves the menu highlight to the OCR'd label, for up to <seconds>.
# [timeout, action, hold, text(, retry)] checks the screen: the action (comma-separated
# presses, or 'wait') is repeated every retry (default RETRY_S) seconds until one of the
# '|'-separated texts appears in a capture taken after it. The run fails (goal_reached false)
# when it does not within the timeout. Some FM4 menus ignore the first press after they open,
# so checked presses are the reliable way; give screens that load a retry longer than the load.
# A '?' before the action skips it when the text already shows (in a capture taken after the
# step starts). A text starting with '=' must be a whole OCR line; '!' checks that none shows.

# Boot to the main menu. A skips the Microsoft Studios and Dolby splash videos (the main menu
# shows after about 30 s instead of 47), one press per screen read: presses at a fixed rate
# also skip the short title screen and select PLAY CAREER in the main menu. The last step
# presses A on the title with a retry longer than the main menu load.
_TITLE = 'start the game|SAVING CONTENT|FORZA MOTORSPORT'
_TO_MAIN_MENU = [
    # Whole lines: the legal notice before the videos also names Microsoft and Studios.
    [45, 'wait', 0, '=Microsoft|=Studios|DOLBY|' + _TITLE], [0.5, 'a', 0.12],
    [20, 'wait', 0, 'DOLBY|' + _TITLE], [0.5, 'a', 0.12],
    [30, '?a', 0.12, 'PLAY CAREER', 8],
]
SCENARIOS = {
    'boot': [[90, 'none', 0]],
    # Main menu -> Free Play -> Quick Race with the default Abarth 131 on Bernese Alps
    # Festival Circuit. The pre-race menu ignores A until its focus has moved once.
    'quickrace': _TO_MAIN_MENU + [
        [4, 'right', 0.12], [1.2, 'right', 0.12], [1.2, 'right', 0.12],
        [2, 'a', 0.12], [6, 'a', 0.12], [8, 'a', 0.12], [5, 'a', 0.12], [6, 'a', 0.12],
        [6, 'a', 0.12], [6, 'a', 0.12], [5, 'a', 0.12], [40, 'a', 0.12],
        [5, 'down', 0.12], [1.5, 'up', 0.12], [1.5, 'a', 0.12], [8, 'rt=255', 0],
    ],
}
# Title screen to the main menu, then into the career home menu (PLAY CAREER). The career menu
# hides after a few idle seconds; the first press shows it again, and Up never wraps.
_TO_CAREER = _TO_MAIN_MENU + [
    [20, 'select:PLAY CAREER', 0],
    [2, 'a', 0.12], [40, 'up', 0.12, 'MY PROFILE'],
]
# Career home menu > GO RACE > WORLD TOUR event screen. Both menus hide after a few idle
# seconds and the first press only shows them again; Up on their top item does nothing else,
# so 'up,a' activates the top item whether the menu shows or not.
_TO_WORLD_TOUR = [
    [30, 'up,a', 0.12, 'WORLD TOUR|EVENT LIST', 10], [60, 'up,a', 0.12, 'DRIVER LEVEL BONUS', 15],
]
SCENARIOS.update({
    # Quick Race, 10 s of driving, then 30 s measured (the race phase that the gates use
    # instead of the quickrace scenario), then pause > PHOTO MODE, take a photo and save it as
    # a new file (the harness answers the name prompt with its default, "Forza"). Use
    # --profile copy.
    'photo': SCENARIOS['quickrace'] + [
        [10, 'wait', 0], [1, 'phase:race', 0], [30, 'wait', 0], [12, 'start', 0.15, 'PHOTO MODE'],
        [1, 'phase:end', 0],
        # OCR often reads nothing in photo mode (a bright scene behind small hints), so entering
        # it is checked by the pause menu closing, and the photo is taken with one A and a fixed
        # wait: it renders for up to about 20 s (LOADING... PLEASE WAIT; no captures at 120 Hz),
        # and a repeated A would open the save dialog. The dark save dialog reads reliably.
        [45, 'select:PHOTO MODE', 0], [20, 'a', 0.12, '!PHOTO MODE|RESUME', 8],
        [2, 'phase:photo', 0], [8, 'wait', 0], [0.2, 'a', 0.12], [25, 'wait', 0],
        [60, '?a', 0.12, 'SAVE PHOTO', 20],
        [30, 'a', 0.12, 'SAVING|SAVE SUCCESSFUL', 10], [30, 'wait', 0, 'SAVE SUCCESSFUL'],
        [1, 'phase:end', 0],
    ],
    # A new process loads the photo saved by 'photo': --profile <photo run>\user-data. Career >
    # MY PROFILE > MEDIA > MY PHOTOS > LOAD shows it full screen. OCR reads its watermark only
    # sometimes, so the check is that no menu, loading text or error follows (and final.png).
    'photo_view': _TO_CAREER + [
        # The career menu hides again within one capture or two at 120 Hz (sooner than at 60 Hz),
        # too fast for a select: Up shows it (or does nothing on GO RACE, where it opens), then
        # four Downs reach MY PROFILE.
        [20, 'up,down,down,down,down,a', 0.12, 'EDIT PLAYERCARD', 20],
        [30, 'select:MEDIA', 0], [15, 'a', 0.12, 'MY PHOTOS'], [30, 'a', 0.12, 'VIEW PHOTOS', 10],
        [15, 'a', 0.12, 'FILE OPTIONS'], [20, 'a', 0.12, '!FILE OPTIONS', 8], [5, 'wait', 0],
        [20, 'wait', 0, '!VIEW PHOTOS|FILE OPTIONS|FAILED|LOADING'], [5, 'wait', 0],
    ],
    # Career > GO RACE > WORLD TOUR: the profile's next event (Tsukuba Short Circuit with the
    # Ford Ka for the BuildRoot profile), 40 s of driving, QUIT RACE, 30 s of its replay, then
    # CONTINUE back to the event. Use --profile copy: quitting changes no progress, but the
    # BuildRoot profile stays untouched either way.
    'career': _TO_CAREER + _TO_WORLD_TOUR + [
        [20, 'a', 0.12, 'START RACE', 10], [20, 'select:START RACE', 0],
        # A skips the intro; a later A may already start the race from the pre-race menu.
        [150, 'a', 0.12, 'STARTING GRID|LAPS', 15], [40, '?down,up,a', 0.12, 'LAPS', 10],
        [1, 'rt=255', 0],
        [1, 'phase:race', 0], [40, 'wait', 0], [12, 'start', 0.15, 'QUIT RACE'],
        [1, 'phase:end', 0],
        # The pause menu sways with the camera (more at 120 Hz), so its highlight can be judged
        # only in some captures: its selects (also PHOTO MODE above) get 45 s.
        [45, 'select:QUIT RACE', 0], [10, 'a', 0.12, 'ARE YOU SURE'],
        # The post-race menu starts the race replay by itself when left idle (sooner at 120 Hz),
        # so it gets fixed presses as soon as it shows: CONTINUE is highlighted, VIEW REPLAY two
        # below. B shows the replay's HUD (REWIND, CAMERA), a second B its menu again.
        [30, 'a', 0.12, 'VIEW REPLAY', 10], [0.2, 'down,down,a', 0.12],
        [5, 'phase:replay', 0], [30, 'wait', 0], [1, 'phase:end', 0],
        [20, 'b', 0.12, 'REWIND|CAMERA', 10], [20, 'b', 0.12, 'CONTINUE'], [0.2, 'a', 0.12],
        [60, 'wait', 0, 'START RACE'],
    ],
})
RETRY_S = 5

# Steps after a goal (--steps <name>): hold the throttle and press Back every 6 s, cycling
# the race cameras (cockpit, hood, bumper, chase near, chase far, back to cockpit).
_CAMERA = [[4, 'rt=255+back', 0.15], [0.2, 'rt=255', 0]] + [[6, 'rt=255+back', 0.15], [0.2, 'rt=255', 0]] * 5
STEP_PRESETS = {'camera': _CAMERA}
# Exact FM4 clear color: where it covers >= 1% of a race capture, geometry such as the road
# surface was not drawn (seen on the RTV path).
CLEAR_GRAY = bytes([102, 102, 102])

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


def read_ppm(ppm):
    parts = ppm.read_bytes().split(b'\n', 3)
    width, height = map(int, parts[1].split())
    return width, height, parts[3]


def ppm_to_png(ppm, png, scale=2):
    write_png(png, *read_ppm(ppm), scale)


def capture_stats(ppm):
    """Luminance statistics of a capture: brightness, black and saturated share, histogram."""
    _, _, rgb = read_ppm(ppm)
    # Rec. 601 luma in integer form; every other pixel is enough for these statistics.
    histogram = [0] * 256
    for r, g, b in zip(rgb[0::6], rgb[1::6], rgb[2::6]):
        histogram[(299 * r + 587 * g + 114 * b) // 1000] += 1
    pixels = sum(histogram) or 1
    return {
        'luma_mean': round(sum(i * n for i, n in enumerate(histogram)) / pixels, 2),
        'black_pct': round(100 * sum(histogram[:8]) / pixels, 2),
        'saturated_pct': round(100 * sum(histogram[248:]) / pixels, 2),
        'luma_hist16': [round(100 * sum(histogram[i:i + 16]) / pixels, 2) for i in range(0, 256, 16)],
    }


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    return values[min(len(values) - 1, int(round(fraction * (len(values) - 1))))]


def set_input(path, buttons=0, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(f'{buttons} {lt} {rt} {lx} {ly} {rx} {ry}\n')
    # The game may be reading the file at this moment; Windows then refuses the replace.
    for attempt in range(50):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            time.sleep(0.01)
    os.replace(tmp, path)


def parse_action(action):
    state = dict(buttons=0, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0)
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


def complete_ppm(path):
    """Whether a capture has been written completely: OCR of a partly written one sees
    a black image and no text."""
    try:
        with open(path, 'rb') as file:
            head = file.read(32).split(b'\n', 3)
        width, height = map(int, head[1].split())
        return path.stat().st_size >= len(b'\n'.join(head[:3])) + 1 + width * height * 3
    except (OSError, ValueError, IndexError):
        return False


def newest_capture(capture, after=0.0):
    frames = [p for p in capture.glob('frame-*.ppm') if p.stat().st_mtime > after]
    frames += [p for p in capture.glob('live-latest.ppm') if p.stat().st_mtime > after]
    frames = [p for p in frames if complete_ppm(p)]
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


def press(input_file, sequence, hold):
    """Taps each comma-separated action: press, hold, release, short pause."""
    for action in sequence.split(','):
        set_input(input_file, **parse_action(action))
        time.sleep(hold or 0.12)
        set_input(input_file)
        time.sleep(0.5)


class Screen:
    """OCR of the newest capture for checked steps; starts the OCR server on first use.
    steps.jsonl records what each check saw."""
    def __init__(self, capture, process, log_path):
        self.capture, self.process, self.ocr = capture, process, None
        self.log_path, self.log_file = log_path, None
        self.frame = None

    def log(self, **record):
        if not self.log_file:
            self.log_file = open(self.log_path, 'w', encoding='utf-8')
        self.log_file.write(json.dumps({'frame': self.frame, **record}) + '\n')
        self.log_file.flush()

    def read(self, after, until):
        """(annotated lines, capture time) of the newest capture written after `after`, waiting
        for one until `until` ((None, after) then); raises when the game exits."""
        while time.time() < until:
            if self.process.poll() is not None:
                raise RuntimeError(f'exited with {self.process.returncode}')
            frame = newest_capture(self.capture, after)
            if frame:
                if not self.ocr:
                    self.ocr = Ocr()
                try:
                    self.frame = frame.name
                    return annotate(self.ocr.read(frame)), frame.stat().st_mtime
                except (RuntimeError, ValueError, OSError) as error:
                    # Usually a capture still being written; a dead server is started again.
                    self.log(ocr_error=repr(error)[:200])
                    if self.ocr.process.poll() is not None:
                        self.ocr = None
            time.sleep(0.2)
        return None, after

    def close(self):
        if self.ocr:
            self.ocr.close()
        if self.log_file:
            self.log_file.close()


def shows(lines, text):
    """Whether any OCR line contains one of the '|'-separated texts (case-insensitive); a text
    starting with '=' must be the whole line. A leading '!' inverts: none of them shows."""
    if text.startswith('!'):
        return not shows(lines, text[1:])
    wanted = [t.strip().upper() for t in text.split('|')]
    return any(w[1:] == l['text'].strip().upper() if w.startswith('=') else w in l['text'].upper()
               for l in lines for w in wanted)


def checked_step(screen, input_file, timeout, action, hold, text, retry=RETRY_S):
    """Repeats the action every `retry` seconds until text shows in a capture taken after it."""
    deadline = time.time() + timeout
    if action.startswith('?'):
        action = action[1:]
        lines, _ = screen.read(time.time(), deadline)
        if lines and shows(lines, text):
            screen.log(check=text, seen=True, skipped=action)
            return True
    while time.time() < deadline:
        if action != 'wait':
            press(input_file, action, hold)
        after = time.time() + 0.3
        retry_until = deadline if action == 'wait' else min(deadline, after + retry)
        while time.time() < retry_until:
            lines, after = screen.read(after, retry_until)
            if lines is not None:
                screen.log(check=text, seen=shows(lines, text), text=[l['text'] for l in lines][:12])
                if shows(lines, text):
                    return True
    return False


def menu_peers(lines, target):
    """Entries of the menu holding target, the ones that may be highlighted, and how to score
    their highlight. In a column the highlighted entry is a dark band (bg about 65 against 110
    and more) with red lines above and below it; the line between two entries belongs to both,
    and a swaying menu can put a line next to a light entry, so only dark entries count (a dark
    submenu header has no red lines). In a row (horizontal menu) tiles have red frames."""
    lines = [l for l in lines if l['y'] < FOOTER_Y]
    column = [l for l in lines if abs(l['x'] - target['x']) <= 40]
    if len(column) >= 2:
        median_bg = sorted(l['bg'] for l in column)[len(column) // 2]
        dark = [l for l in column if l['bg'] <= 0.8 * median_bg]
        return column, dark, lambda l: min(l.get('red_above', 0), l.get('red_below', 0))
    row = [l for l in lines if abs(l['y'] - target['y']) <= 12]
    return row, row, lambda l: max(l['red'], l['red_text'])


def select_label(screen, input_file, timeout, label):
    """Moves the highlight to label. Highlight detection as in annotate(), but only among the
    target's menu, so red elsewhere (a logo, the player's row) does not count. It acts only
    when two clear readings in a row (captures of a swaying menu in between give none) find
    the same entry highlighted. Not for menus that hide when idle: a press would only show
    them again."""
    deadline = time.time() + timeout
    after = time.time()
    previous = None
    while time.time() < deadline:
        lines, after = screen.read(after, deadline)
        if not lines:
            continue
        target = next((l for l in lines if l['text'].strip().upper() == label.upper()), None) or \
            next((l for l in lines if label.upper() in l['text'].upper() and l['y'] < FOOTER_Y), None)
        if not target:
            screen.log(select=label, target=None, text=[l['text'] for l in lines][:12])
            continue
        peers, candidates, score = menu_peers(lines, target)
        ranked = sorted(candidates, key=lambda l: -score(l))
        screen.log(select=label, target=[target['x'], target['y']],
                   peers=[[l['text'], l['x'], l['y'], l['bg'], score(l), l in candidates] for l in peers])
        if not ranked or score(ranked[0]) < 15 or \
                (len(ranked) > 1 and score(ranked[0]) < 1.3 * score(ranked[1])):
            continue
        current = ranked[0]
        # While a menu slides in, its red lines can sit next to another entry for a capture.
        steady, previous = previous == current['text'], current['text']
        if not steady:
            continue
        if current is target:
            return True
        axis, (back, forward) = ('x', ('left', 'right')) if abs(current['y'] - target['y']) <= 12 \
            else ('y', ('up', 'down'))
        low, high = sorted((current[axis], target[axis]))
        steps = 1 + sum(1 for l in peers if low < l[axis] < high)
        press(input_file, ','.join([forward if target[axis] > current[axis] else back] * steps), 0.12)
        after = time.time() + 0.3
        previous = None
    return False


def perf_lines(path):
    """Windows written to the perf trace so far: marks where a phase starts."""
    try:
        return path.read_bytes().count(b'\n')
    except OSError:
        return 0


def prepare_profile(source, build_root, out):
    """Copies a user-data root to <out>/user-data, so saves and progress of the run stay out of
    the BuildRoot profile. The installed content packages (0000000000000000, gigabytes, never
    written by the game) are linked from the BuildRoot instead of copied."""
    content = '0000000000000000'
    target = out / 'user-data'
    if target.exists():
        # rmdir removes only the link and refuses a real non-empty directory, so the BuildRoot
        # packages are never deleted through it.
        if os.path.lexists(target / content):
            os.rmdir(target / content)
        shutil.rmtree(target)
    shutil.copytree(source, target, ignore=lambda directory, names:
                    [content] if Path(directory) == Path(source) else [])
    packages = build_root / 'user-data' / content
    if packages.is_dir():
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(target / content), str(packages)],
                       check=True, capture_output=True)
    return target


def summarize(perf, measure):
    """Per-run statistics over the steady part of the perf trace."""
    steady = perf[-measure:] if measure else (perf[len(perf) // 3:] if len(perf) > 6 else perf)
    if not steady:
        return {}
    per_s = lambda p, value: value / p['window_ms'] * 1000
    median = lambda values: round(statistics.median(values), 2)
    frames = [f for p in steady for f in p.get('frame_ms', [])]
    minutes = sum(p['window_ms'] for p in steady) / 60000
    result = {
        'fps_median': round(statistics.median([p['fps'] for p in steady]), 1),
        'fps_min': round(min(p['fps'] for p in steady), 1),
        'wait_ms_per_s_median': {k: round(statistics.median([per_s(p, p['wait_ms'][k]) for p in steady]), 1)
                                 for k in steady[0]['wait_ms']},
        # Wall time of the command processor thread not spent waiting.
        'cp_busy_ms_per_s_median': round(statistics.median(
            [per_s(p, p['window_ms'] - sum(p['wait_ms'].values())) for p in steady]), 1),
        'readbacks_per_s_median': round(statistics.median([p['readbacks'] for p in steady]), 1),
        'draws_per_s_median': round(statistics.median([p['draws'] for p in steady]), 1),
    }
    if frames:
        result.update({
            # Frames over time across all measured windows; steadier than the median of
            # per-second FPS, which jumps between 20, 24 and 30 as frames snap to vblanks.
            'fps_mean': round(1000 * len(frames) / sum(frames), 2),
            'frame_ms_p50': round(percentile(frames, 0.50), 2),
            'frame_ms_p95': round(percentile(frames, 0.95), 2),
            'frame_ms_p99': round(percentile(frames, 0.99), 2),
            'hitch_50ms_per_min': round(sum(f > 50 for f in frames) / minutes, 2),
            'hitch_100ms_per_min': round(sum(f > 100 for f in frames) / minutes, 2),
        })
    if 'cpu_ms' in steady[0]:
        # Command-processor wall time per work kind, inclusive (draw contains draw_*).
        result['cpu_ms_per_s_median'] = {k: median([per_s(p, p['cpu_ms'][k]) for p in steady])
                                         for k in steady[0]['cpu_ms']}
    if 'occlusion_samples' in steady[0]:
        # Mean passing samples per occlusion interval as reported to the game.
        result['occlusion_samples_median'] = {k: median([p['occlusion_samples'][k] for p in steady])
                                              for k in steady[0]['occlusion_samples']}
    gpu = [p['gpu'] for p in steady if p.get('gpu', {}).get('frames')]
    if gpu:
        # GPU time per frame from timestamps: busy (submissions), resolves, first-to-last span.
        result['gpu_frame_ms_median'] = median([g['frame_ms'] for g in gpu])
        result['gpu_resolve_ms_median'] = median([g['resolve_ms'] for g in gpu])
        result['gpu_span_ms_median'] = median([g['span_ms'] for g in gpu])
    if 'counts' in steady[0]:
        frames_in = lambda p: max(1, len(p.get('frame_ms', [])))
        result['counts_per_s_median'] = {k: median([per_s(p, p['counts'][k]) for p in steady])
                                         for k in steady[0]['counts']}
        result['counts_per_frame_median'] = {k: median([p['counts'][k] / frames_in(p) for p in steady])
                                             for k in steady[0]['counts']}
    return result


def run_once(options, out):
    root = Path(options.build_root)
    out.mkdir(parents=True, exist_ok=True)
    capture = out / 'capture'
    capture.mkdir(exist_ok=True)
    for old in list(capture.glob('*')) + [out / 'perf.jsonl', out / 'runtime.log', out / 'nav.jsonl',
                                          out / 'steps.jsonl']:
        if old.is_file():
            old.unlink()
    state = json.loads((root / 'local.json').read_text())
    input_file = out / 'input.txt'
    set_input(input_file)

    # Same cvars as Build.ps1 -Action run.
    sys.path.insert(0, str(HERE.parent))
    import pipeline
    args = {k: str(v) for k, v in pipeline.launch_cvars(root, state['disc1'], out / 'runtime.log').items()}
    # Nobody can type into the host keyboard dialog: XAM prompts answer with their defaults.
    args['headless'] = 'true'
    if options.profile:
        source = root / 'user-data' if options.profile == 'copy' else Path(options.profile)
        args['user_data_root'] = str(prepare_profile(source, root, out))
    for item in options.arg:
        key, value = item.lstrip('-').split('=', 1)
        args[key] = value
    command = [str(root / options.game_build / 'fm4_rex.exe')] + [f'--{k}={v}' for k, v in args.items()]
    env = {k: v for k, v in os.environ.items() if not k.startswith(('FM4_', 'REX_'))}
    env.update(pipeline.launch_env())
    env.update(FM4_CAPTURE_DIR=str(capture),
               FM4_TEST_INPUT_FILE=str(input_file), REX_PERF_TRACE_FILE=str(out / 'perf.jsonl'))
    # Diagnostic variables, e.g. REX_RESOLVE_PUBLICATION_TRACE=<path>.
    env.update(item.split('=', 1) for item in options.env)
    # With --goal, --steps run after the goal is reached (e.g. looking around in the race).
    if options.steps in STEP_PRESETS:
        steps = STEP_PRESETS[options.steps]
    else:
        steps = json.loads(options.steps) if options.steps else ([] if options.goal else SCENARIOS[options.scenario])

    stderr = open(out / 'stderr.log', 'w', encoding='utf-8', errors='replace')
    started = time.time()
    process = subprocess.Popen(command, cwd=root, env=env, stdout=stderr, stderr=subprocess.STDOUT)
    timeline = []
    crashed = None
    goal_reached = None
    failed_step = None
    phases = []
    screen = Screen(capture, process, out / 'steps.jsonl')
    try:
        if options.goal:
            goal_reached = navigate(options.goal, process, input_file, capture, out,
                                    started + options.goal_timeout, started)
            if goal_reached:
                set_input(input_file, rt=255)
        for index, (wait, action, hold, *text) in enumerate(steps):
            if text:
                done = checked_step(screen, input_file, wait, action, hold, *text)
                timeline.append([round(time.time() - started, 1), action, text[0], done])
            elif action.startswith('select:'):
                done = select_label(screen, input_file, wait, action[7:])
                timeline.append([round(time.time() - started, 1), action, done])
            else:
                deadline = time.time() + wait
                while time.time() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'exited with {process.returncode}')
                    time.sleep(0.1)
                done = True
                if action.startswith('phase:'):
                    phases.append((action[6:], perf_lines(out / 'perf.jsonl')))
                elif ',' in action:
                    press(input_file, action, hold)
                elif action != 'wait':
                    set_input(input_file, **parse_action(action))
                    if hold:
                        time.sleep(hold)
                        set_input(input_file)
                timeline.append([round(time.time() - started, 1), action])
            if not done:
                failed_step = f'step {index}: {action}' + (f' -> {text[0]!r}' if text else '') + ' timed out'
                break
            if text or action.startswith('select:'):
                goal_reached = True if goal_reached is None else goal_reached
        if failed_step:
            goal_reached = False
        deadline = time.time() + (0 if failed_step else options.tail)
        while time.time() < deadline and process.poll() is None:
            time.sleep(0.1)
        if process.poll() is not None:
            raise RuntimeError(f'exited with {process.returncode}')
    except RuntimeError as error:
        crashed = str(error)
    except Exception as error:
        crashed = f'harness error: {error!r}'
    finally:
        screen.close()
        if process.poll() is None:
            process.kill()
            process.wait()
        stderr.close()

    frames = sorted(capture.glob('frame-*.ppm'))
    final_stats = None
    if frames:
        # Keep the final capture under a stable name for checkpoints.
        ppm_to_png(frames[-1], out / 'final.png')
        final_stats = capture_stats(frames[-1])
    clear_gray_frames = []
    for frame in frames:
        width, height, rgb = read_ppm(frame)
        # Runs of the clear color count once per pixel; misaligned matches are negligible.
        if rgb.count(CLEAR_GRAY) >= 0.01 * width * height:
            clear_gray_frames.append(frame.stem)
        ppm_to_png(frame, frame.with_suffix('.png'))
        if not options.keep_ppm:
            frame.unlink()

    perf = [json.loads(line) for line in (out / 'perf.jsonl').read_text().splitlines()] \
        if (out / 'perf.jsonl').is_file() else []
    log_text = (out / 'runtime.log').read_text(encoding='utf-8', errors='replace') \
        if (out / 'runtime.log').is_file() else ''
    errors = {}
    for line in log_text.splitlines():
        if re.search(r'\b(E|error|ERROR)\b|\[error\]|\[E\]', line):
            key = re.sub(r'^\S+\s+\S+\s+', '', line)[:90]
            key = re.sub(r'0x[0-9A-Fa-f]+|\d+', '#', key)
            errors[key] = errors.get(key, 0) + 1
    # Each phase covers the perf windows from its mark to the next mark.
    bounds = phases + [('end', len(perf))]
    phase_stats = {name: summarize(perf[start:end], end - start)
                   for (name, start), (_, end) in zip(bounds, bounds[1:]) if name != 'end' and end > start}
    summary = {
        'crashed': crashed, 'goal': options.goal, 'scenario': None if options.steps else options.scenario,
        'goal_reached': goal_reached, 'failed_step': failed_step, 'args': options.arg, 'profile': options.profile,
        'seconds': round(time.time() - started, 1), 'timeline': timeline,
        'frames_captured': len(frames), 'perf_windows': len(perf),
        **summarize(perf, options.measure),
        'phases': phase_stats,
        'final_capture': final_stats,
        'clear_gray_frames': clear_gray_frames,
        'errors': errors,
        'top_errors': sorted(errors.items(), key=lambda kv: -kv[1])[:10],
    }
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    return summary


PHASE_KEYS = ('fps_mean', 'fps_median', 'frame_ms_p50', 'frame_ms_p95', 'frame_ms_p99', 'hitch_50ms_per_min',
              'cp_busy_ms_per_s_median', 'draws_per_s_median', 'gpu_frame_ms_median')


def aggregate(runs):
    """Combines repeated runs: median of medians, minimum of minimums, union of errors."""
    ok = [r for r in runs if 'fps_median' in r]
    median = lambda values: round(statistics.median(values), 2) if values else None
    pick = lambda key: [r[key] for r in ok if r.get(key) is not None]
    nested = lambda key: {k: median([r[key][k] for r in ok if key in r]) for k in ok[0].get(key, {})} if ok else {}
    errors = {}
    for run in runs:
        for key, count in run.get('errors', {}).items():
            errors[key] = errors.get(key, 0) + count
    summary = {
        'repeat': len(runs),
        'runs': [dict(crashed=r['crashed'], goal_reached=r['goal_reached'], fps_median=r.get('fps_median'))
                 for r in runs],
        'crashed': next((r['crashed'] for r in runs if r['crashed']), None),
        'goal': runs[0]['goal'], 'scenario': runs[0].get('scenario'), 'args': runs[0]['args'],
        'failed_steps': [r.get('failed_step') for r in runs],
        'goal_reached': None if runs[0]['goal_reached'] is None else all(r['goal_reached'] for r in runs),
        'fps_median': median(pick('fps_median')),
        'fps_min': min(pick('fps_min'), default=None),
        'cp_busy_ms_per_s_median': median(pick('cp_busy_ms_per_s_median')),
        'readbacks_per_s_median': median(pick('readbacks_per_s_median')),
        'draws_per_s_median': median(pick('draws_per_s_median')),
        'wait_ms_per_s_median': nested('wait_ms_per_s_median'),
        'counts_per_s_median': nested('counts_per_s_median'),
        'counts_per_frame_median': nested('counts_per_frame_median'),
        'cpu_ms_per_s_median': nested('cpu_ms_per_s_median'),
        'occlusion_samples_median': nested('occlusion_samples_median'),
        'final_captures': [r['final_capture'] for r in runs],
        'clear_gray_frames': sum(len(r.get('clear_gray_frames', [])) for r in runs),
        'errors': errors,
        'top_errors': sorted(errors.items(), key=lambda kv: -kv[1])[:10],
    }
    for key in ('fps_mean', 'frame_ms_p50', 'frame_ms_p95', 'frame_ms_p99', 'hitch_50ms_per_min', 'hitch_100ms_per_min',
                'gpu_frame_ms_median', 'gpu_resolve_ms_median', 'gpu_span_ms_median'):
        summary[key] = median(pick(key))
    phase_names = list(dict.fromkeys(name for r in ok for name in (r.get('phases') or {})))
    summary['phases'] = {name: {key: median([r['phases'][name][key] for r in ok
                                             if r['phases'].get(name, {}).get(key) is not None])
                                for key in PHASE_KEYS}
                         for name in phase_names}
    return summary


def resummarize(out, measure):
    """Refreshes perf statistics of finished runs, e.g. after new summary fields were added."""
    def refresh(directory):
        summary = json.loads((directory / 'summary.json').read_text())
        perf = [json.loads(line) for line in (directory / 'perf.jsonl').read_text().splitlines()]
        summary.update(summarize(perf, measure))
        (directory / 'summary.json').write_text(json.dumps(summary, indent=2))
        return summary
    runs = sorted(out.glob('run[0-9]*'), key=lambda d: int(d.name[3:]))
    if not runs:
        return refresh(out)
    summary = aggregate([refresh(run) for run in runs])
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-root', required=True, help='BuildRoot used by Build.ps1')
    parser.add_argument('--game-build', default='game-build',
                        help='build directory under BuildRoot, e.g. game-build-thinlto (pipeline.py --flavor)')
    parser.add_argument('--out', required=True, help='result directory, outside the repository')
    parser.add_argument('--scenario', default='boot', choices=sorted(SCENARIOS),
                        help='fixed timeline: boot, quickrace, photo, photo_view, career')
    parser.add_argument('--profile', help="copy this user-data root to <out>/user-data for the run "
                                          "('copy': the BuildRoot one); the BuildRoot profile stays as it is")
    parser.add_argument('--goal', choices=sorted(GOALS), help='navigate with Jev instead of a timeline')
    parser.add_argument('--goal-timeout', type=float, default=360)
    parser.add_argument('--env', action='append', default=[], help='extra NAME=value for the game process')
    parser.add_argument('--steps', help='JSON list of steps or a preset name (camera), overrides --scenario')
    parser.add_argument('--arg', action='append', default=[], help='extra/override --cvar=value')
    parser.add_argument('--tail', type=float, default=10, help='seconds to keep running after the last step')
    parser.add_argument('--keep-ppm', action='store_true', help='keep full-resolution captures')
    parser.add_argument('--measure', type=int, default=0, help='summarize only the last N perf seconds')
    parser.add_argument('--repeat', type=int, default=1,
                        help='run N times into <out>/run1..runN and aggregate into <out>/summary.json')
    parser.add_argument('--resummarize', action='store_true',
                        help='recompute summary.json of existing runs in --out from their perf.jsonl')
    options = parser.parse_args()

    out = Path(options.out).resolve()
    # Captures and logs show game content; they must never land in the repository.
    if out == REPO or REPO in out.parents:
        parser.error('--out must be outside the repository, for example <BuildRoot>/e2e/<name>')
    if options.resummarize:
        summary = resummarize(out, options.measure)
    elif options.repeat <= 1:
        summary = run_once(options, out)
    else:
        runs = []
        for index in range(1, options.repeat + 1):
            runs.append(run_once(options, out / f'run{index}'))
            print(f'run{index}: fps_median={runs[-1].get("fps_median")} crashed={runs[-1]["crashed"]}',
                  flush=True)
        summary = aggregate(runs)
        (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps({k: v for k, v in summary.items() if k != 'errors'}, indent=2))
    if summary['crashed'] or summary['goal_reached'] is False:
        sys.exit(1)


if __name__ == '__main__':
    main()
