"""Non-destructive local HTTP integration smoke test. Start app first."""
import json
import re
import time
import urllib.request
import urllib.error

BASE = 'http://127.0.0.1:8765'


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=10) as response:
        return response.read()


def main():
    html = get('/').decode('utf-8')
    assert 'ESP-Werkbank' in html
    token = re.search(r"const token='([^']+)'", html)[1]
    state = json.loads(get('/api/state'))
    print('HTTP / HTML / STATE OK; ports:', [p['device'] for p in state['ports']])
    # Invalid requests must be rejected before any hardware access.
    req = urllib.request.Request(BASE + '/api/action', data=b'{"action":"install"}', headers={'Content-Type': 'application/json'})
    try:
        urllib.request.urlopen(req, timeout=5)
        raise AssertionError('CSRF guard missing')
    except urllib.error.HTTPError as exc:
        assert exc.code == 403
    body = json.dumps({'action': 'document'}).encode()
    req = urllib.request.Request(BASE + '/api/action', data=body,
                                 headers={'Content-Type': 'application/json', 'X-Connecty-Token': token})
    with urllib.request.urlopen(req, timeout=5) as response:
        assert response.status == 202
    for _ in range(100):
        state = json.loads(get('/api/state'))
        if not state['busy']:
            break
        time.sleep(.05)
    assert not state['busy'] and not state['error'], state['error']
    for file in state['files']:
        assert get(file['url'])
    assert len(state['files']) == 5
    print('TOKEN / DOCUMENT ACTION / FIVE REPORT FILES OK')


if __name__ == '__main__':
    main()
