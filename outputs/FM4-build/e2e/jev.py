"""Minimal TypeSafe System One client (Jev) using only the standard library."""
import json, os, time, urllib.error, urllib.request

API_URL = 'https://api.typesafe.ai/v1/systemone'


def api_key():
    key = os.environ.get('TYPESAFE_API_KEY')
    if not key and os.name == 'nt':
        # Sessions started before the key was saved don't inherit it.
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, 'Environment') as env:
            key = winreg.QueryValueEx(env, 'TYPESAFE_API_KEY')[0]
    if not key:
        raise RuntimeError('TYPESAFE_API_KEY is not set')
    return key


def system_one(state, questions, model='jev-latest', retries=4):
    body = json.dumps({'model': model, 'state': state, 'questions': questions}).encode()
    request = urllib.request.Request(API_URL, data=body, method='POST', headers={
        'Authorization': 'Bearer ' + api_key(), 'Content-Type': 'application/json'})
    for attempt in range(retries + 1):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.loads(response.read())
        except urllib.error.HTTPError as error:
            if error.code in (429, 529) and attempt < retries:
                time.sleep(0.5 * 2 ** attempt)
                continue
            raise RuntimeError(f'Jev HTTP {error.code}: {error.read()[:300]!r}') from None
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            # Brief network drops (e.g. host unreachable) should not end a long run.
            if attempt < retries:
                time.sleep(2 * 2 ** attempt)
                continue
            raise
