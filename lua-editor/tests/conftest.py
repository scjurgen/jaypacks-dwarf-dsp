import os

# no window may appear while the tests run
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
