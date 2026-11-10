"""One lock for the x64 builds that share objects: the engine's, the design engine's and the
studio's. A build waits while another holds it, so two sessions never compile or link the same
objects at once, and takes it over when its holder has been gone longer than any build takes."""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LOCK = os.path.join(HERE, "..", "src117", "build", "builds.lock")
WAIT_S = 1800


class BuildLock:
    def __init__(self, who):
        self.who = who
        self.held = False

    def __enter__(self):
        # a build run by another build that already holds the lock goes straight on
        if os.environ.get("HX_BUILD_LOCK_HELD") == "1":
            return self
        os.makedirs(os.path.dirname(LOCK), exist_ok=True)
        start = time.time()
        said = False
        while True:
            try:
                fd = os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                os.write(fd, ("%d %s" % (os.getpid(), self.who)).encode())
                os.close(fd)
                self.held = True
                os.environ["HX_BUILD_LOCK_HELD"] = "1"
                return self
            except FileExistsError:
                try:
                    age = time.time() - os.path.getmtime(LOCK)
                    holder = open(LOCK).read().strip()
                except OSError:
                    continue
                if age > WAIT_S:
                    print("[%s] a build lock %d s old taken over (%s)" % (self.who, age, holder), flush=True)
                    try:
                        os.remove(LOCK)
                    except OSError:
                        pass
                    continue
                if not said:
                    print("[%s] waiting for another build (%s)" % (self.who, holder), flush=True)
                    said = True
                if time.time() - start > WAIT_S:
                    sys.exit("[%s] another build holds %s" % (self.who, LOCK))
                time.sleep(2.0)

    def __exit__(self, *exc):
        if self.held:
            os.environ.pop("HX_BUILD_LOCK_HELD", None)
            try:
                os.remove(LOCK)
            except OSError:
                pass
