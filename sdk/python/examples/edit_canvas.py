"""AGORA_USER_TOKEN=<token> python examples/edit_canvas.py <canvas-id>"""
import os,sys
from agora_canvas import AgoraClient
with AgoraClient(os.getenv('AGORA_URL','https://frelog.p-e.kr'),token=os.environ['AGORA_USER_TOKEN']) as client:
 with client.canvas(int(sys.argv[1]),password=os.getenv('AGORA_CANVAS_PASSWORD')) as canvas:
  key=canvas.create({'kind':'shape','shapeType':'rectangle','x':.25,'y':.25,'width':.15,'height':.12,'fill':'#ddd5ff'})
  canvas.patch(key,{'fill':'#b5e5d0'})
  print('Created and updated:',key,'revision:',canvas.revision)
  canvas.delete(key)
  print('Deleted example object')
