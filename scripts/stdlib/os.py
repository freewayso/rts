# Minimal os for Permafrost's trimmed Python stdlib.
# Full CPython os.py needs the 'nt'/'errno' C modules which this build omits.

name = 'nt'
sep = '\\'
altsep = '/'
pathsep = ';'
linesep = '\r\n'
curdir = '.'
pardir = '..'
extsep = '.'
defpath = '.'
devnull = 'nul'

SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2

class _Environ(object):
    def __init__(self):
        self._data = {}

    def __getitem__(self, key):
        if key in self._data:
            return self._data[key]
        raise KeyError(key)

    def get(self, key, default=None):
        return self._data.get(key, default)

    def __contains__(self, key):
        return key in self._data

    def __setitem__(self, key, value):
        self._data[key] = value

    def __delitem__(self, key):
        del self._data[key]

    def keys(self):
        return self._data.keys()

    def items(self):
        return self._data.items()

    def values(self):
        return self._data.values()

environ = _Environ()

def getenv(key, default=None):
    return environ.get(key, default)

def putenv(key, value):
    environ[key] = value

def getcwd():
    return '.'

def getcwdu():
    return u'.'

class _PathModule(object):
    sep = '\\'
    altsep = '/'
    curdir = '.'
    pardir = '..'
    extsep = '.'

    def join(self, a, *p):
        path = a
        for b in p:
            if (b.startswith('\\') or b.startswith('/')
                    or (len(b) >= 2 and b[1] == ':')):
                path = b
            elif not path or path.endswith('\\') or path.endswith('/'):
                path = path + b
            else:
                path = path + '\\' + b
        return path

    def basename(self, p):
        p = p.replace('/', '\\')
        i = p.rfind('\\')
        return p[i + 1:]

    def dirname(self, p):
        p = p.replace('/', '\\')
        i = p.rfind('\\')
        if i < 0:
            return ''
        return p[:i]

    def split(self, p):
        return (self.dirname(p), self.basename(p))

    def splitext(self, p):
        i = p.rfind('.')
        j = p.rfind('\\')
        if i < 0 or i < j:
            return (p, '')
        return (p[:i], p[i:])

    def exists(self, p):
        try:
            f = open(p, 'rb')
            f.close()
            return True
        except Exception:
            return False

    def isfile(self, p):
        return self.exists(p)

    def isdir(self, p):
        return False

    def normpath(self, p):
        return p.replace('/', '\\')

    def abspath(self, p):
        if len(p) >= 2 and p[1] == ':':
            return self.normpath(p)
        return self.normpath(self.join(getcwd(), p))

path = _PathModule()
error = OSError
