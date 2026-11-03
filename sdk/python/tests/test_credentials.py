import os,tempfile,unittest
from pathlib import Path
from agora_canvas import AgoraClient
from agora_canvas.credentials import save_bot_credentials,load_bot_credentials
class CredentialsTests(unittest.TestCase):
 def test_save_load_and_reconnect_configuration(self):
  with tempfile.TemporaryDirectory() as directory:
   path=Path(directory)/'bot.token.json';created={'bot':{'canvas_id':42,'user_id':9},'token':'agora_bot_9.'+'a'*43}
   save_bot_credentials(path,created,base_url='https://frelog.p-e.kr')
   self.assertEqual(path.stat().st_mode&0o777,0o600);self.assertEqual(load_bot_credentials(path)['token'],created['token'])
   with AgoraClient.from_bot_file(path) as client:
    self.assertEqual(client.bot_canvas_id,42);self.assertNotIn(created['token'],repr(client))
   with self.assertRaises(FileExistsError):save_bot_credentials(path,created,base_url='https://frelog.p-e.kr')
 def test_reject_shared_and_symlinked_files(self):
  if os.name!='posix':self.skipTest('POSIX file permissions')
  with tempfile.TemporaryDirectory() as directory:
   path=Path(directory)/'bot.token.json';save_bot_credentials(path,{'bot':{'canvas_id':42},'token':'agora_bot_9.'+'a'*43},base_url='https://frelog.p-e.kr')
   path.chmod(0o644)
   with self.assertRaises(ValueError):load_bot_credentials(path)
   path.chmod(0o600);link=Path(directory)/'link';link.symlink_to(path)
   with self.assertRaises(ValueError):load_bot_credentials(link)
