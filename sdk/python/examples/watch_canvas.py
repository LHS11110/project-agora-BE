"""Join as a normal participant and observe authorized document/presence events."""
import argparse
import os
from agora_canvas import AgoraClient, AgoraError


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('canvas_id',type=int)
    parser.add_argument('--show-cursor',action='store_true',help='Publish one visible cursor at (.25,.25)')
    args=parser.parse_args()
    if args.canvas_id<=0:parser.error('canvas_id must be positive')
    token=os.environ.get('AGORA_BOT_TOKEN') or os.environ.get('AGORA_USER_TOKEN')
    if not token:parser.error('Set AGORA_BOT_TOKEN or AGORA_USER_TOKEN')
    options={'base_url':os.environ.get('AGORA_URL','https://frelog.p-e.kr')}
    client=AgoraClient.from_bot(token,**options) if token.startswith('agora_bot_') else AgoraClient(token=token,**options)
    try:
        with client,client.canvas(args.canvas_id,password=os.environ.get('AGORA_CANVAS_PASSWORD')) as canvas:
            print('Connected:',canvas.peer_id,'participants:',len(canvas.peers)+1,flush=True)
            if args.show_cursor:canvas.send_cursor(.25,.25)
            while True:
                event=canvas.next_event(timeout=5)
                if not event:continue
                kind=event['type']
                if kind=='canvas_commit':
                    print('objects:',event['changed_ids'],'author:',event.get('author_id'),'revision:',event['revision'],flush=True)
                elif kind=='canvas_relay':
                    peer,payload=event['peer'],event['payload']
                    print(payload['type'],peer['nickname'],peer['tag_number'],payload,flush=True)
                elif kind=='event_overflow':
                    print('Events skipped:',event['dropped'],'latest revision:',canvas.revision,flush=True)
                elif kind in ('rtc_peers','rtc_peer_joined','rtc_peer_left','chat','canvas_access_changed','canvas_settings_changed'):
                    print(kind,event,flush=True)
    except KeyboardInterrupt:pass
    except AgoraError as error:raise SystemExit(f'Canvas error: {error.code or error.status or type(error).__name__}') from None


if __name__=='__main__':main()
