#!/usr/bin/env python3
"""Pass an in-memory Forgejo identity to the native Git credential prompt."""

import os
import sys

print(os.environ["FORGEJO_GIT_USERNAME" if "username" in sys.argv[1].lower() else "FORGEJO_GIT_TOKEN"])
