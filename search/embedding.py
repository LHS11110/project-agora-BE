import os
from pathlib import Path
import threading
import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer


class E5:
    dimensions = 384
    def __init__(self, path='/models/e5'):
        root = Path(path)
        self.revision = (root / 'revision').read_text().strip()
        options = ort.SessionOptions()
        options.intra_op_num_threads = int(os.environ.get('EMBEDDING_THREADS', '2'))
        options.inter_op_num_threads = 1
        if not 1 <= options.intra_op_num_threads <= 16: raise ValueError('Embedding threads must be 1..16')
        self.session = ort.InferenceSession(str(root / 'model.onnx'), sess_options=options, providers=['CPUExecutionProvider'])
        self.tokenizer = Tokenizer.from_file(str(root / 'tokenizer.json'))
        self.tokenizer.enable_truncation(max_length=512)
        self.tokenizer.enable_padding(pad_id=1, pad_token='<pad>')
        self.lock = threading.Lock()
    def encode(self, texts, kind='query'):
        if not isinstance(texts, list) or kind not in ('query', 'passage') or not 1 <= len(texts) <= 8: raise ValueError('Invalid embedding batch')
        if any(not isinstance(text, str) or not text.strip() or len(text) > 8192 for text in texts):
            raise ValueError('Text must contain 1..8192 characters')
        if not self.lock.acquire(timeout=3): raise TimeoutError('Embedding busy')
        try:
            tokens = self.tokenizer.encode_batch([kind + ': ' + text for text in texts])
            arrays = {'input_ids': np.array([item.ids for item in tokens], dtype=np.int64),
                      'attention_mask': np.array([item.attention_mask for item in tokens], dtype=np.int64),
                      'token_type_ids': np.array([item.type_ids for item in tokens], dtype=np.int64)}
            hidden = self.session.run(None, {item.name: arrays[item.name] for item in self.session.get_inputs()})[0]
            mask = arrays['attention_mask'][..., None]
            pooled = (hidden * mask).sum(axis=1) / np.maximum(mask.sum(axis=1), 1)
            norm = np.linalg.norm(pooled, axis=1, keepdims=True)
            if pooled.shape[1] != self.dimensions or not np.isfinite(pooled).all() or (norm == 0).any():
                raise RuntimeError('Invalid model output')
            return (pooled / norm).astype(np.float32).tolist()
        finally: self.lock.release()
