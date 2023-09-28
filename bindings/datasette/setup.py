from setuptools import setup

version = {}
with open("datasette_sqlite_tg/version.py") as fp:
    exec(fp.read(), version)

VERSION = version["__version__"]


setup(
    name="datasette-sqlite-tg",
    description="",
    long_description="",
    long_description_content_type="text/markdown",
    author="Alex Garcia",
    url="https://github.com/asg017/sqlite-tg",
    project_urls={
        "Issues": "https://github.com/asg017/sqlite-tg/issues",
        "CI": "https://github.com/asg017/sqlite-tg/actions",
        "Changelog": "https://github.com/asg017/sqlite-tg/releases",
    },
    license="MIT License, Apache License, Version 2.0",
    version=VERSION,
    packages=["datasette_sqlite_tg"],
    entry_points={"datasette": ["sqlite_tg = datasette_sqlite_tg"]},
    install_requires=["datasette", "sqlite-tg"],
    extras_require={"test": ["pytest"]},
    python_requires=">=3.6",
)
