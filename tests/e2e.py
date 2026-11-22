#!/usr/bin/env python3
"""Runs the PC build of the payload against a stand-in server and a fake console folder.

Covers the whole conversation: link code -> key, sync, "send me this trophy set",
second sync, a revoked key, and a manually installed key.
"""
import http.server, json, os, shutil, subprocess, sys, threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', 'build', 'fakeroot-e2e')
BIN = os.path.join(HERE, '..', 'build', 'trophyshare-host')
sys.path.insert(0, HERE)
import make_fixture

state = {'codes': {}, 'keys': {}, 'games': {}, 'synced': [], 'polls': 0, 'log': []}


class Api(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a):
        pass

    def reply(self, status, obj, chunked=False):
        data = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Connection', 'close')
        if chunked:  # the real server may answer in chunks: make sure the client copes
            self.send_header('Transfer-Encoding', 'chunked')
            self.end_headers()
            for i in range(0, len(data), 7):
                part = data[i:i + 7]
                self.wfile.write(b'%x\r\n%s\r\n' % (len(part), part))
            self.wfile.write(b'0\r\n\r\n')
        else:
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        self.close_connection = True

    def handle_any(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        auth = self.headers.get('Authorization', '')
        key = auth[7:] if auth.startswith('Bearer ') else ''
        state['log'].append((self.command, self.path))
        assert self.headers.get('User-Agent', '').startswith('ps5-trophy-share/'), self.headers.get('User-Agent')
        data = json.loads(body) if body else {}

        if self.path == '/v1/link/start':
            code = 'ABCD%04d' % len(state['codes'])
            state['codes'][code] = {'secret': 's3cret' + code, 'label': data.get('label')}
            return self.reply(200, {'ok': True, 'code': code, 'secret': 's3cret' + code, 'show': code[:4] + '-' + code[4:],
                                    'url': 'https://site.test/link/', 'expires_in': 30, 'interval': 1})
        if self.path == '/v1/link/poll':
            c = state['codes'].get(data.get('code'))
            if not c or c['secret'] != data.get('secret'):
                return self.reply(404, {'ok': False, 'error': 'bad_code', 'message': 'Unknown code.'})
            state['polls'] += 1
            if state['polls'] < 2:
                return self.reply(200, {'ok': True, 'status': 'pending'})
            key = 'tsk_key_for_' + data['code']
            state['keys'][key] = {'earned': {}}
            del state['codes'][data['code']]
            return self.reply(200, {'ok': True, 'status': 'linked', 'key': key, 'player': {'id': '7K3M9QXZ', 'nickname': 'Téster "One"'}})

        if key not in state['keys']:
            return self.reply(401, {'ok': False, 'error': 'bad_key', 'message': 'This console is not linked any more.'})
        if self.path == '/v1/sync':
            need, stored, fresh = [], 0, 0
            for s in data['sets']:
                if s['np'] not in state['games']:
                    need.append(s['np'])
                    continue
                old = state['keys'][key]['earned'].get(s['np'], [])
                if old != s['earned']:
                    stored += 1
                    fresh += len(s['earned']) - len(old)
                state['keys'][key]['earned'][s['np']] = s['earned']
            state['synced'].append(data['sets'])
            total = sum(len(e) for e in state['keys'][key]['earned'].values())
            return self.reply(200, {'ok': True, 'need': need, 'need_icon': [], 'stored': stored, 'new_trophies': fresh,
                                    'player': {'id': '7K3M9QXZ', 'nickname': 'Téster'},
                                    'totals': {'p': 0, 'g': 1, 's': 0, 'b': total - 1, 'games': len(state['keys'][key]['earned'])},
                                    'url': 'https://site.test/u/7K3M9QXZ'}, chunked=True)
        if self.command == 'PUT' and self.path.startswith('/v1/games/'):
            np = self.path.split('/')[3]
            assert data['conf']['trophyNpCommId'] == np
            state['games'][np] = data
            return self.reply(200, {'ok': True, 'stored': True})
        return self.reply(404, {'ok': False, 'error': 'not_found'})

    do_GET = do_POST = do_PUT = handle_any


def run(user, expect=0):
    env = dict(os.environ, TS_ROOT=ROOT, TS_API='http://127.0.0.1:%d/' % PORT, TS_USER=user)
    p = subprocess.run([BIN, '--once'], env=env, capture_output=True, text=True, timeout=120)
    if p.returncode != expect:
        print(p.stdout, p.stderr)
        raise SystemExit('payload exited with %d, expected %d' % (p.returncode, expect))
    return p.stdout


def check(cond, what):
    if not cond:
        raise SystemExit('FAIL: ' + what)
    print('ok  ', what)


shutil.rmtree(ROOT, ignore_errors=True)
make_fixture.main(ROOT)
server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Api)
PORT = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()
keyfile = os.path.join(ROOT, 'data/trophyshare/keys/10000001')

# 1. first run: link, then send everything
out = run('10000001:Tester')
check('type the code ABCD-0000' in out and 'open site.test/link/' in out, 'the code and the address are shown on screen')
check(open(keyfile).read().strip() == 'tsk_key_for_ABCD0000', 'the key is saved for that user')
check(oct(os.stat(keyfile).st_mode & 0o777) == '0o600', 'the key file is private')
check('now linked to Téster "One"' in out, 'the nickname comes through with its accents and quotes')
check(sorted(state['games']) == ['NPWR90001_00', 'NPWR90002_00'], 'both trophy sets were described to the server')
check(state['games']['NPWR90001_00']['lang'] == 'en-US', 'English names are preferred')
check(state['games']['NPWR90002_00']['lang'] == 'ja-JP', 'else the set\'s default language')
check(state['games']['NPWR90001_00']['meta']['metadata']['titleMetadata']['name'] == 'Phosphor Quest', 'the names file is sent whole')
last = {s['np']: s for s in state['synced'][-1]}
check(last['NPWR90001_00'] == {'np': 'NPWR90001_00', 'rev': 100, 'earned': [[1, 1790000000], [4, 1790000500], [5, 1790001000]]},
      'earned trophies carry their IDs and times')
check(last['NPWR90002_00']['earned'] == [] and last['NPWR90002_00']['rev'] == 7, 'a set with nothing earned is still sent')
check('3 trophies from 2 games are on your page' in out, 'the first sync is reported on screen')

check(not any('10000002' in json.dumps(s) for s in state['synced']), 'the other user was left alone')
check(len(state['synced']) == 2, 'two sync calls: before and after describing the sets')

# 2. second run: already linked, nothing to describe
before = len(state['log'])
out = run('10000001:Tester')
calls = state['log'][before:]
check(calls == [('POST', '/v1/sync')], 'a later run is a single sync call')
check('type the code' not in out, 'and shows no code')

# 3. the console was removed on the site
state['keys'].clear()
out = run('10000001:Tester', expect=1)
check(not os.path.exists(keyfile), 'a key the server refuses is deleted')

# 4. a key made by hand on the site, saved as /data/trophyshare/key
state['keys']['tsk_by_hand'] = {'earned': {}}
with open(os.path.join(ROOT, 'data/trophyshare/key'), 'w') as f:
    f.write('  tsk_by_hand \r\n')
out = run('10000002:Second')
check(open(os.path.join(ROOT, 'data/trophyshare/keys/10000002')).read().strip() == 'tsk_by_hand', 'a hand-made key goes to the signed-in user')
check(not os.path.exists(os.path.join(ROOT, 'data/trophyshare/key')), 'and the loose file is removed')
check(state['keys']['tsk_by_hand']['earned'] == {'NPWR90003_00': [[0, 1791000000]]}, 'that user\'s trophies were sent')
check(state['games']['NPWR90003_00']['lang'] == 'en-GB', 'British English counts as English')

# 5. the server is down
server.shutdown()
server.server_close()
out = run('10000002:Second', expect=1)
check('Cannot reach the server' in out, 'an unreachable server is reported, not fatal')
print('end-to-end tests passed')
