"""Device-wide resource leases across all joined realms.

Logical expiry does not prove that a GPU transfer/process stopped. Resource
release requires the managed child to exit. Crash recovery verifies a Linux
process birth stamp before touching a PID; ambiguous reservations remain held.
"""
import os
import time
from pathlib import Path
from .codec import decode,integer
from .storage import Database
from .errors import require


def process_birth(pid):
    try:
        text=Path(f"/proc/{pid}/stat").read_text()
        return text[text.rfind(")")+2:].split()[19]
    except (OSError,IndexError):return None


class Governor:
    def __init__(self,data:Path):
        self.data=Path(data);self.db=Database(self.data/"resources.sqlite3")
        self.db.db.execute("CREATE TABLE IF NOT EXISTS resource_lease(id TEXT PRIMARY KEY,realm TEXT,owner_pid INTEGER,child_pid INTEGER,birth TEXT,slots INTEGER,vram INTEGER,state TEXT)")
    def policy(self):return decode((self.data/"config.json").read_bytes())["resource_policy"]
    def acquire(self,id,realm,vram=0):
        p=self.policy();require(p["idle_contribution"] is True,"COMPUTE_NOT_AUTHORIZED")
        with self.db.tx() as c:
            rows=c.execute("SELECT slots,vram FROM resource_lease WHERE state='ACTIVE'").fetchall()
            require(sum(r[0] for r in rows)+1<=p["slots"],"DEVICE_SLOTS")
            require(sum(r[1] for r in rows)+vram<=p["vram_bytes"],"DEVICE_VRAM")
            c.execute("INSERT INTO resource_lease VALUES(?,?,?,?,?,?,?,?)",(id,realm,os.getpid(),0,"",1,vram,"ACTIVE"))
        return p
    def started(self,id,pid):
        with self.db.tx() as c:c.execute("UPDATE resource_lease SET child_pid=?,birth=? WHERE id=? AND state='ACTIVE'",(pid,process_birth(pid) or "UNKNOWN",id))
    def release(self,id,*,quiescent):
        require(quiescent,"PHYSICAL_QUIESCENCE_REQUIRED")
        with self.db.tx() as c:c.execute("UPDATE resource_lease SET state='QUIESCENT' WHERE id=?",(id,))
    def reconcile(self):
        released=[];ambiguous=[]
        with self.db.tx() as c:
            for r in c.execute("SELECT * FROM resource_lease WHERE state='ACTIVE'").fetchall():
                if r["child_pid"]==0:
                    # Owner may still be between reservation and spawn.
                    try:os.kill(r["owner_pid"],0);alive=True
                    except ProcessLookupError:alive=False
                    except PermissionError:alive=True
                    if alive:ambiguous.append(r["id"]);continue
                else:
                    birth=process_birth(r["child_pid"])
                    if birth==r["birth"] or (birth is None and r["birth"]=="UNKNOWN"):
                        ambiguous.append(r["id"]);continue
                c.execute("UPDATE resource_lease SET state='QUIESCENT' WHERE id=?",(r["id"],));released.append(r["id"])
        return {"released":released,"ambiguous_still_charged":ambiguous}
    def close(self):self.db.close()
