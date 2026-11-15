"""Manual live-provider smoke test with an isolated in-memory C++ tool fixture.
No real canvas or database records are modified. Requires the deployed agent.
"""
import argparse
import json
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true', required=True)
    parser.add_argument('--socket', default='/run/agora-agent/agent.sock')
    args = parser.parse_args()
    store = {'test-code': {'revision': 1, 'item': {'kind': 'text', 'renderMode': 'code', 'language': 'python', 'text': 'def add(a, b):\n    return a + b\n'}}}
    calls = []
    request = {'operation': 'run', 'canvas_id': 0, 'message': '격리된 테스트입니다. test-code 객체를 get_objects로 읽고 기존 add 함수를 유지하면서 multiply(a, b) 함수를 추가해 patch_objects로 저장해주세요. 이어서 print("agent-test-ok")를 담은 새 Python 코드 객체 하나를 create_objects로 생성해주세요. 삭제하거나 코드를 실행하지 마세요. 실제 도구 성공 여부만 간단히 보고해주세요.', 'selected_ids': ['test-code'], 'history': [], 'allow_delete': False, 'anchor': {'x': .2, 'y': .2}}
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(225)
        connection.connect(args.socket)
        with connection.makefile('rwb') as stream:
            def send(value):
                stream.write((json.dumps(value, ensure_ascii=False) + '\n').encode())
                stream.flush()
            send(request)
            start = time.monotonic()
            while True:
                line = stream.readline()
                if not line:
                    raise RuntimeError('Agent disconnected')
                frame = json.loads(line)
                if frame.get('type') == 'output':
                    print(frame.get('output', '').strip(), flush=True)
                    continue
                if frame.get('type') == 'tool':
                    name, arguments = frame['name'], frame['arguments']
                    calls.append(name)
                    if name == 'get_objects':
                        result = {'objects': [{'id': key, **store[key]} for key in arguments['ids'] if key in store]}
                    elif name == 'patch_objects':
                        ids = []
                        for change in arguments['changes']:
                            assert change['id'] == 'test-code' and change['revision'] == store['test-code']['revision']
                            assert 'get_objects' in calls
                            store['test-code']['item'].update(change['patch'])
                            store['test-code']['revision'] += 1
                            ids.append(change['id'])
                        result = {'object_ids': ids, 'revision': 2}
                    elif name == 'create_objects':
                        ids = []
                        for item in arguments['items']:
                            key = 'new-' + str(len(store))
                            store[key] = {'revision': 1, 'item': item}
                            ids.append(key)
                        result = {'object_ids': ids, 'revision': 3}
                    else:
                        result = {'error': 'TEST_TOOL_NOT_ALLOWED'}
                    send({'type': 'tool_result', 'id': frame['id'], 'result': result})
                    continue
                print(json.dumps({'elapsed_seconds': round(time.monotonic() - start, 2), 'tools': calls, 'result': frame}, ensure_ascii=False), flush=True)
                assert 'error' not in frame, frame.get('error')
                source = store['test-code']['item']['text']
                assert 'def add' in source and 'def multiply' in source
                assert any('agent-test-ok' in value['item'].get('text', value['item'].get('code', '')) for key, value in store.items() if key != 'test-code')
                assert all(name in calls for name in ['get_objects', 'patch_objects', 'create_objects'])
                print('PASS: real EXAONE + MCP read / revision-checked patch / create. Existing canvas data unchanged.')
                return

if __name__ == '__main__':
    main()
