# The documentation site

A static site built from the Markdown the tools already carry — the top-level
`README.md` and `NOTES.md`, everything under `docs/`, and each `src/pdp11-*/`
`README.md`. Nothing is duplicated here: editing a document is the only step,
and there is no second copy to keep in step with the first.

```
gh-pages/
  site.json        the site model: metadata, the tool groups and the tool list,
                   and the curated top-level + apsim pages (each tool's own
                   README, and docs/design.md / docs/user-guide.md if present,
                   are discovered)
  build_site.py    the generator (markdown-it-py); writes public/
  check_links.py   fails if the built site links to something it lacks
  templates/       home.html, doc.html, page.html
  assets/          site.css, logo-pdp11.svg
  public/          build output — committed here and rebuilt/published by Actions
```

## Build it locally

```sh
pip install markdown-it-py
python3 gh-pages/build_site.py
python3 gh-pages/check_links.py
python3 -m http.server -d gh-pages/public 8000   # then open localhost:8000
```

The GitHub Actions workflow (`.github/workflows/pages.yml`) runs exactly these
two Python steps and publishes `gh-pages/public/` to Pages. The built site is
also committed to the repo, so rebuild and commit it whenever you change a
source document (a `make docs` target does both). Set **Settings → Pages →
Source** to *GitHub Actions* once.

## Serving it locally over Apache (optional)

To read the built site alongside the sibling projects over a shared development
vhost — the same plain-HTTP arrangement `tappty`, `forterp` and `vax11-xdev`
use, so it needs no click-through certificate warning — wire it the same way
they do. The vhost points at `gh-pages/public/` **directly**, so it serves
whatever was built last; after editing any Markdown, rebuild:

```sh
python3 gh-pages/build_site.py && python3 gh-pages/check_links.py
```

The wiring lives outside this repo, in two places (mirror the `vax11-xdev`
entries, substituting `pdp11`):

| File | Contains |
|---|---|
| `/etc/apache2/conf-available/pdp11-xdev-repo.conf` | `Define PDP11_XDEV_REPO` — the path to this checkout |
| the shared dev vhost | the `Alias`, `<Directory>` and `<Location>` blocks, on both vhosts |

Write the `Define` file, `a2enconf pdp11-xdev-repo`, add the blocks the way the
neighbouring tools have them, then `apachectl configtest && systemctl reload
apache2`. The vhost refuses to start with a named error if the `Define` is
missing, rather than falling back to serving something unexpected.

## Adding documentation

A tool's own `README.md` (and `docs/design.md` / `docs/user-guide.md` if it
grows them) is **discovered from the tree** — adding one of those files is the
only step, and it appears on the tool's card automatically. Registering a new
*tool* is one entry in `site.json`'s `tools` array (`id`, `name`, `tagline`,
`blurb`, and the `group` it belongs to; the groups themselves are the ordered
`groups` array).

Everything else — the top-level pages, the apsim syscall docs, a page in a
non-standard place — is an explicit entry in the `docs` array:

```json
{ "slug": "cross-headers", "title": "The cross/ headers", "source": "docs/cross-headers.md",
  "summary": "One sentence for the card and the meta description.", "featured": true }
```

`template` is `doc` (a left rail of the page's own H2s, shown one section at a
time) or `page` (one continuous scroll, for indexes and short documents);
`featured` promotes it to a card on the home page. Two pages that resolve to the
same slug fail the build rather than silently overwriting each other.

## What the generator does to a document

- **Cross-document `.md` links are rewritten** to the built page's URL, resolved
  by **path** (not basename — every tool carries a `README.md`), so the same
  link works when the file is read on GitHub and when it is read here.
- **Links to source files** — a probe, a header, a Makefile — are rewritten to
  a GitHub blob URL, because they have no counterpart in the site. A link to a
  file that does not exist is left alone, so it stays visible as the mistake it
  is instead of becoming a plausible dead URL.
- **The first `# H1` is removed** from the body and used as the page title, so
  a document does not show its own title twice.
- **`<!--include: path-->`** is replaced by that file's current contents in a
  fenced block, for showing source without copying it.
