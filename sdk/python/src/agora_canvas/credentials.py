"""Local credential files. Never store these files in a canvas or source control."""
import json
import os
from pathlib import Path
import stat


def save_bot_credentials(path, created, *, base_url):
    """Exclusively create a 0600 file; refuse overwrite and symlinks."""
    path=Path(path).expanduser()
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    record={"version":1,"base_url":base_url,"bot":created["bot"],"token":created["token"]}
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    try:
        with os.fdopen(fd,'w',encoding='utf-8') as output:
            json.dump(record,output,ensure_ascii=False)
            output.flush();os.fsync(output.fileno())
    except BaseException:
        path.unlink(missing_ok=True)
        raise
    return path


def load_bot_credentials(path):
    """Load a bounded, regular, owner-only file without following a symlink."""
    path=Path(path).expanduser()
    if path.is_symlink():raise ValueError("Credential file must not be a symlink")
    fd=os.open(path,os.O_RDONLY|getattr(os,'O_NOFOLLOW',0))
    with os.fdopen(fd,'r',encoding='utf-8') as source:
        info=os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size>8192:raise ValueError("Invalid credential file")
        if os.name=='posix' and (stat.S_IMODE(info.st_mode)&0o077 or info.st_uid!=os.geteuid()):raise ValueError("Credential file must be owned by you with mode 0600")
        record=json.load(source)
    if record.get('version')!=1 or not isinstance(record.get('token'),str) or not record['token'].startswith('agora_bot_') or type(record.get('bot',{}).get('canvas_id')) is not int:
        raise ValueError("Invalid bot credentials")
    return record
