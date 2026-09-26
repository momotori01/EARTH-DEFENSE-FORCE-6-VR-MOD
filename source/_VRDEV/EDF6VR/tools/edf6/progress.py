"""What the person watching the window sees while the pack is built.

A run takes about half an hour and spends most of it inside one call to the
upscaler, which says nothing until it is finished. Left alone that is minutes of
a still screen, which is indistinguishable from a hang, so the folder the
upscaler writes into is counted instead: the count is the truth about how far it
has got and it needs no cooperation from the program doing the work.

One line does all of it, rewritten in place: a bar that only ever grows, what is
happening right now, and how much longer. The bar moves inside a file as well as
between files, because a single city map can take a minute and a bar that sat
still for that long would be no better than silence.

Everything is plain ASCII. The console this runs in may be on a Japanese code
page, where a box-drawing character would come out as mojibake.

Printing is flushed on every line. Python block-buffers its output when it is
not talking to a console, and the pack may well be started from a script that
redirects it, where a buffer would hold the whole run and release it at the end.
"""
import os
import sys
import threading
import time

WIDTH = 22          # characters inside the bar
LINE = 79           # keep clear of an 80-column console's wrap


def say(text=''):
    sys.stdout.write(text[:LINE] + '\n')
    sys.stdout.flush()


class Run:
    """The whole build: one growing bar, and an estimate of what is left.

    Progress is measured in bytes of source archive, not in files. The archives
    differ by a factor of a thousand, so counting files would show a bar nearly
    full while the three largest maps were still to come.
    """

    def __init__(self, total_bytes, total_files):
        self.total_bytes = max(1, total_bytes)
        self.total_files = total_files
        self.done_bytes = 0
        self.done_files = 0
        self.started = time.time()
        self.current_size = 0
        self.current_share = 0.0        # how far through the file being worked on
        self.what = ''
        self._lock = threading.Lock()
        self._stop = None
        self._thread = None

    # -- the bar itself ---------------------------------------------------

    def fraction(self):
        done = self.done_bytes + self.current_size * self.current_share
        return min(1.0, done / float(self.total_bytes))

    def _left(self):
        spent = time.time() - self.started
        share = self.fraction()
        if share < 0.02 or spent < 15:
            return ''
        remaining = spent / share - spent
        if remaining < 90:
            return 'nearly done'
        return 'about %d min left' % round(remaining / 60.0)

    def draw(self):
        share = self.fraction()
        filled = int(round(share * WIDTH))
        bar = '#' * filled + '.' * (WIDTH - filled)
        text = '  [%s] %3d%%  %-22s %s' % (bar, round(share * 100), self.what, self._left())
        sys.stdout.write('\r' + text[:LINE].ljust(LINE))
        sys.stdout.flush()

    def clear(self):
        sys.stdout.write('\r' + ' ' * LINE + '\r')
        sys.stdout.flush()

    # -- one file ---------------------------------------------------------

    def begin(self, size, name, index):
        self.current_size = size
        self.current_share = 0.0
        self.what = ''
        say('[%d of %d]  %s' % (index, self.total_files, name))
        self.draw()

    def complete(self):
        """A full bar, left on screen, before the closing lines."""
        self.done_bytes = self.total_bytes
        self.current_size = 0
        self.what = 'finished'
        self.draw()
        say()

    def end(self, note=''):
        self.done_bytes += self.current_size
        self.done_files += 1
        self.current_size = 0
        self.current_share = 0.0
        self.clear()
        if note:
            say('   ' + note)

    # -- one step inside a file -------------------------------------------

    def step(self, label, folder, expected, first, last):
        """Counts `folder` while the step runs, moving the bar from first to last."""
        return _Step(self, label, folder, expected, first, last)


class _Step:
    def __init__(self, run, label, folder, expected, first, last):
        self.run = run
        self.label = label
        self.folder = folder
        self.expected = expected
        self.first = first
        self.last = last
        self._stop = threading.Event()
        self._thread = None

    def _count(self):
        try:
            return len(os.listdir(self.folder))
        except OSError:
            return 0

    def _loop(self):
        while not self._stop.wait(0.5):
            done = min(self._count(), self.expected)
            with self.run._lock:
                self.run.current_share = self.first + (self.last - self.first) * (
                    done / float(self.expected))
                self.run.what = '%s %d of %d' % (self.label, done, self.expected)
                self.run.draw()

    def __enter__(self):
        with self.run._lock:
            self.run.current_share = self.first
            self.run.what = '%s 0 of %d' % (self.label, self.expected)
            self.run.draw()
        if self.expected:
            self._thread = threading.Thread(target=self._loop, daemon=True)
            self._thread.start()
        return self

    def __exit__(self, *_):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        with self.run._lock:
            self.run.current_share = self.last
            self.run.draw()
        return False
