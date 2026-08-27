# Forwarding an HTML message with its formatting

Item 171. Design, 2026-08-27. **Read this before the backlog row**, which
records only the cause.

## The defect

`ComposeContextBuilder::quoteBody()` reads `ParsedMessage::plainBody` and
nothing else, so the forward path drops the original's `htmlBody` entirely.
Two consequences, and they are not the same severity:

- An original with both parts forwards its text/plain alternative. The words
  survive, the sender's formatting does not. This is what the user reported.
- **An original with an HTML part ONLY has an empty `plainBody`**, so the
  forward carries an attribution line and an empty quote. The content is gone
  and nothing says so.

Measured on the developer's own inbox 2026-08-27: **30 of 342 sampled
messages** (~9%) declare `text/html` with no `text/plain` part. The silent
half is not an edge case.

## What was decided, and by whom

The user chose, 2026-08-27, from three routes put to them:

1. **Inline, carrying the original's markup** — CHOSEN. The forward carries
   the original's own HTML rather than a flattened quote. Highest inline
   fidelity, and the most work, because it is the only route that puts a
   stranger's markup into an outgoing message.

   **Amended 2026-08-27, after the first build**: a forward sends ONE part,
   not a `multipart/alternative`. The Send-as-HTML toggle chooses which — the
   original's markup when on, the text quote when off. The user's reasoning is
   that a forward's shape is something they have already decided by flipping
   that toggle, and sending both halves hands the choice to the recipient's
   client instead. An ordinary (non-forward) message still sends the
   alternative as before; only the forward path is single-part.
2. Attach the original as `message/rfc822` (item 130's mechanism). Rejected
   here, though item 130 may still build it for its own sake: the user wants
   the content inline, not as an attachment.
3. Text fallback only. Rejected: it fixes the silent-loss half and does not
   answer the note at all, since formatting is still lost on every forward.

**Remote content is STRIPPED BY DEFAULT, with a per-forward opt-out**, also
the user's choice against "always strip" and "keep everything". A control in
the composer, checked by default, reading roughly "Strip remote content from
the forwarded message".

## Why this is the security-critical item in the backlog

Every other HTML path in this application renders a stranger's markup *to the
user*, behind protections that live in `MessageView`: an off-the-record
profile, JavaScript disabled, and `RequestInterceptor` blocking every request
by default and failing closed.

**None of those protections apply here.** The markup leaves this process and
is rendered by somebody else's mail client, under their policy, on their
machine. The interceptor cannot help: it intercepts requests *we* would make.
So the sanitising has to happen to the bytes, before they are handed to
`MessageSender`, and there is no second line of defence behind it.

The concrete harm, in the user's own words when the decision was put to them:
forwarding a tracking pixel forwards the tracking. The original sender learns
that the forwarded copy was opened, by whom, and how many times, and the
user's recipient never consented to that.

## The allow-list rule, which is not negotiable

`HtmlBuilder::namespaceCids()` is the closest prior art and it is a
**block-list**: it names the attributes that can carry a `cid:` and rewrites
those. It documents scoping `srcset=` out, on the reasoning that its quoting
grammar differs and `cid:` in `srcset` is not seen in the wild.

**That trade is correct for rewriting and WRONG for stripping**, and the
asymmetry is the whole design:

| | a missed reference means |
|---|---|
| `namespaceCids` (rewrite) | one broken image |
| this sanitiser (strip) | a tracking beacon reaching the recipient |

So the sanitiser must **allow-list what may remain**, not block-list what must
go. Anything not recognised is removed. A new HTML attribute, a quoting form
not anticipated, a `srcset`, a CSS `image-set()`, an `@import` — each is
handled by the default, which is removal, rather than by having been
enumerated in advance.

Stated as the invariant to test against: **after sanitising, no attribute
value and no CSS construct in the output may contain a URL whose scheme is
anything other than `cid:`, and no element that fetches may remain without
one.**

## What is kept and what goes

Kept:

- `cid:` references. They travel inside the message, fetch nothing, and are
  what makes an inline logo survive. Item 129 will need the same machinery.
- Structural and presentational markup: tables, lists, headings, spans,
  `style=""` attributes with their remote constructs removed.

Removed:

- Any `src`, `href`, `background`, `poster`, `srcset`, `data-*` or other
  attribute value carrying a non-`cid:` URL scheme. `http:`, `https:`,
  `//host/path`, `data:` (which can carry markup), `file:` above all.
- `<link>`, `<script>`, `<iframe>`, `<object>`, `<embed>`, `<base>`, and
  `<meta http-equiv="refresh">`.
- CSS `url()`, `@import` and `image-set()` naming anything but a `cid:`,
  whether in a `style=""` attribute or a `<style>` block.
- Event-handler attributes (`onload`, `onerror`, …). The recipient's client
  most likely disables scripting, but that is their policy and not ours to
  assume.

A stripped `<img>` leaves a gap where the image was. That is the correct
outcome and should not be papered over with a placeholder that itself fetches.

## Shape of the change

- **`HtmlSanitiser`, a new namespace of free functions over values**
  (`src/htmlsanitiser.h/.cpp`), matching `MarkdownRenderer` and
  `MessageBuilder`. No widget, so the security property is testable without a
  painter or a web engine. This is where the allow-list lives.
- **`OutgoingMessage` gains the forwarded HTML and the strip flag.** It
  currently carries `markdownBody` and `sendHtml` only, so there is nowhere to
  put a second HTML source; both are new fields rather than a changed call
  site.
- **`MessageBuilder::build()` chooses the part.** On a forward with the toggle
  ON, the body is one `text/html` part: the user's rendered markdown, a rule,
  then the sanitised original. With the toggle OFF it is one `text/plain` part
  carrying the markdown and the text quote. No alternative either way.

  **The toggle is honoured even when the original had no plain-text part**, so
  an HTML-only original forwarded with the toggle off goes out as the text
  fallback and its formatting is lost. Chosen deliberately 2026-08-27 over
  forcing HTML for those messages, so that the toggle means what it says; the
  first build forced it and that was reversed.
- **`quoteBody()` is not the fix and must not become it.** It is shared with
  Reply, and the behaviour asked for is the forward's alone. It keeps
  producing the text quote for the plain half. Its one change is the
  silent-loss case: when `plainBody` is empty it should render `htmlBody` down
  to text rather than emitting an empty quote, so the plain half is never
  blank.
- **The composer control** sits with the existing per-message toggles, checked
  by default, and only appears on a Forward carrying HTML.

## Traps recorded in advance

- **`quoteBody()` is shared with Reply.** A change there reaches both paths.
- **`QString::arg()` does not collapse `%%`.** CLAUDE.md records the 0.11.0
  placeholder losing its mask, its glow and both gradients this way while
  still painting a plausible pane. Any generated CSS here is subject to it.
- **A geometry or rendering probe cannot see this.** The property under test
  is "no remote URL survives", which is a property of the STRING. Assert on
  the generated bytes. CLAUDE.md's "Rendering probes lie" section is the
  general warning; here a probe that renders the result and looks at it would
  endorse a tracking pixel it cannot see, because a 1x1 transparent image is
  invisible by design.
- **Test with real hostile shapes**, not tidy markup: unquoted attribute
  values (`<img src=http://x/p>`), mixed quoting, whitespace and newlines
  around `=`, uppercase tags and attributes, a `cid:` whose id contains a
  URL-looking substring, and a `style` attribute carrying `url(...)` with no
  quotes. `namespaceCids()` documents each of these forms as real.
- **`MimeParser` must parse what we build.** A round trip through
  `MessageBuilder` and back is the check that the nesting is right, and it is
  cheaper than reading the RFC.

## Order of work

1. `HtmlSanitiser` with the allow-list, tested against the hostile shapes
   above. Nothing else depends on decisions inside it.
2. `quoteBody()`'s empty-plain fallback, which closes the silent-loss half on
   its own and is independently useful.
3. `OutgoingMessage` fields and the `MessageBuilder` nesting, with a
   parse-back round trip.
4. The composer control, defaulting to strip.
5. Hand test: forward a real HTML message to yourself with the box checked and
   unchecked, and confirm what arrives.

Steps 1 and 2 are separable and each ship a real improvement, so this does not
have to land as one commit.
