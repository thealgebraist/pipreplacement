import sys
import os
import modulefinder


def get_deps(script):
    finder = modulefinder.ModuleFinder()
    finder.run_script(script)
    res = []
    for name, mod in finder.modules.items():
        if mod.__file__:
            res.append(os.path.abspath(mod.__file__))
    return res


if __name__ == "__main__":
    deps = get_deps(sys.argv[1])
    for d in deps:
        print(d)
