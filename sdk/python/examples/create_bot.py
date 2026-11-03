"""AGORA_USER_TOKEN=<manager-token> python examples/create_bot.py <canvas-id> <name> <secret-file>"""
import json,os,sys
from agora_canvas import AgoraClient
with AgoraClient(os.getenv('AGORA_URL','https://frelog.p-e.kr'),token=os.environ['AGORA_USER_TOKEN']) as owner:
 with owner.canvas(int(sys.argv[1]),password=os.getenv('AGORA_CANVAS_PASSWORD')) as canvas:
  created=canvas.create_bot(sys.argv[2])
  # Exclusive creation avoids overwriting an existing secret or following a symlink.
  try:
   fd=os.open(sys.argv[3],os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
   with os.fdopen(fd,'w') as output:json.dump(created,output)
  except BaseException:
   canvas.revoke_bot(created['bot']['user_id'])
   raise
  print('Bot participant created; credential saved privately. User ID:',created['bot']['user_id'])
