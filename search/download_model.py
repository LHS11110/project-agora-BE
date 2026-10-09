"""Pinned public model files only. No remote Python code or API credentials."""
from pathlib import Path
import ssl
import urllib.request
from urllib.parse import urlsplit


class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urlsplit(newurl).scheme != "https":
            raise RuntimeError("Model download redirects must remain HTTPS")
        return super().redirect_request(req, fp, code, msg, headers, newurl)

MODEL = 'intfloat/multilingual-e5-small'
REVISION = '614241f622f53c4eeff9890bdc4f31cfecc418b3'
context = ssl.create_default_context()
context.minimum_version = ssl.TLSVersion.TLSv1_3
opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=context), HTTPSRedirect())
root = Path('/models/e5'); root.mkdir(parents=True, exist_ok=True)
for remote, local in [('onnx/model.onnx', 'model.onnx'), ('onnx/tokenizer.json', 'tokenizer.json'), ('README.md', 'MODEL_CARD.md')]:
    url = f'https://huggingface.co/{MODEL}/resolve/{REVISION}/{remote}'
    with opener.open(url, timeout=300) as response, (root / local).open('wb') as output:
        if not response.url.startswith('https://'): raise RuntimeError('Model download must remain HTTPS')
        while chunk := response.read(1024 * 1024): output.write(chunk)
(root / 'revision').write_text(MODEL + '@' + REVISION)
