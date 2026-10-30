import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "test-fw"))
sys.path.insert(0, os.path.join(ROOT, "host"))
