#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša (after fm1-base's tools/publish_site.sh, by Charles Vestal)
# Build the site for release VERSION (tools/site.sh) and push it to the gh-pages branch of the repository fm1.toml
# names (repo), replacing what was there (the branch holds only the generated site); then ask GitHub Pages to build
# it, after turning Pages on for gh-pages if it is not on yet (gh, logged in).
#   tools/publish.sh VERSION
#   REMOTE   push somewhere else (default: fm1.toml's repo)
set -e
cd "$(dirname "$0")/.."
V="${1:?usage: tools/publish.sh VERSION}"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "publish.sh: uncommitted changes: the site is made from a commit (commit them first)"
    exit 1
fi
REPO_URL="$(sed -n 's/^repo *= *"\([^"]*\)".*/\1/p' app/fm1.toml)"
REMOTE="${REMOTE:-$REPO_URL.git}"
SHA="$(git rev-parse --short HEAD)"
sh tools/site.sh "$V"
cd app/build/site
rm -rf .git
git init -q -b gh-pages
git add -A
git commit -q -m "Site for Berlinan for the FM-1 $V (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
# Pages from the gh-pages branch, then a build now: a push alone has left the first deploy stuck on "building"
REPO="$(printf %s "$REMOTE" | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')"
gh api "repos/$REPO/pages" >/dev/null 2>&1 ||
    gh api -X POST "repos/$REPO/pages" -f "source[branch]=gh-pages" -f "source[path]=/" >/dev/null 2>&1 ||
    echo "(could not turn GitHub Pages on: in the repository's settings, Pages, deploy from the gh-pages branch)"
gh api -X POST "repos/$REPO/pages/builds" >/dev/null 2>&1 || echo "(could not ask GitHub Pages to build: check the repository's Pages settings)"
echo "pushed the site for Berlinan $V to gh-pages of $REPO: https://jiripisa.github.io/fm1-berlinan/"
