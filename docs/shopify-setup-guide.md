# SPASynth Shopify setup guide

Plain-English, non-technical walkthrough for setting up the SPASynth products
on Shopify. Files to upload live in `dist/shopify/` (gitignored, local only);
the copy to paste lives in `docs/shopify-listings.md`. House style: no em dashes.

## Before you start

1. Shopify admin login (`yourstore.myshopify.com/admin`).
2. The files: `dist/shopify/SPASynth-Standard-<version>/` and
   `dist/shopify/SPASynth-Pro-<version>/` (open in Finder). Use the current
   version folder (1.0.5 at time of writing, but always use whatever's
   newest). Note: the newest folders are often installer-iteration builds
   with an empty `Library/` subfolder inside them by design; if the library
   zip is missing, clone it in from the most recent folder that has one
   (1.0.3/1.0.2 as of this writing) before uploading.
3. The copy: `docs/shopify-listings.md` (titles, descriptions, SEO, blurbs).
4. Prices:

| Product | Price | Compare-at price |
| --- | --- | --- |
| Standard | 99 | 149 |
| Pro | 499 | 899 |
| Upgrade | 400 | 750 |

Price = paid today; Compare-at = the higher struck-through number.

## Part 1: Turn on digital downloads (one time)

1. Settings (bottom-left) -> Apps and sales channels -> Shopify App Store.
2. Search "Digital Products" (the free official Shopify app, formerly
   "Digital Downloads"), pick the one by Shopify, Install.
3. Confirm a payment provider exists: Settings -> Payments (Shopify Payments
   or PayPal). Set one up before going live if none.

## Part 2: Create SPASynth Standard

1. Products -> Add product.
2. Title: `SPASynth Standard`.
3. Description: paste the "1 · SPASynth Standard" section from
   `docs/shopify-listings.md`.
4. Media: upload `docs/spasynth-marketing.png` (+ logo optionally).
5. Pricing: Price `99`, Compare-at price `149`.
6. Inventory: UNCHECK "Track quantity".
7. Shipping: UNCHECK "This is a physical product" (critical; removes shipping).
8. Search engine listing -> Edit: paste Standard's Page title + Meta
   description; URL handle `spasynth-standard`.
9. Status: leave on Draft for now.
10. Save.

## Part 3: Attach the files (Standard)

1. Apps -> Digital Products -> select SPASynth Standard.
2. Upload these six files from the current `SPASynth-Standard-<version>/`
   folder (1.0.5 at time of writing), one at a time:
   - `SPASynth-<version>-macOS.pkg`
   - `SPASynth-<version>-Windows.exe`
   - `SPASynth Starter Library.zip` (~3 GB, in the `Library` subfolder; if
     that folder is empty, clone the zip in from the most recent folder that
     has one before uploading)
   - `README.txt`, `QUICKSTART.txt`, `EULA.txt`
   Never wrap them in one giant zip; attach individually.

## Part 4: Create SPASynth Pro

The 37 GB Pro library is too big for Shopify, so it lives on Cloudflare R2 and
is delivered as a small self-contained HTML page of download buttons
(`SPASynth Pro Library - Downloads.html`; buyers open it and click the 11
buttons). You do NOT upload the library to Shopify. (The Digital Products app
has an "external URL" asset type, but it only supports Notion / Google
Drive-Docs / Dropbox / YouTube / Vimeo / Figma etc., NOT a raw R2 link, so the
HTML page is uploaded as a file asset instead.)

Repeat Parts 2 and 3 with:
- Title `SPASynth Pro`; description = "2 · SPASynth Pro" section.
- Price `499`, Compare-at `899`; URL handle `spasynth-pro`.
- Files from the current `SPASynth-Pro-<version>/` folder (1.0.5 at time of
  writing; all small, upload directly): pkg, exe, the 3 docs, and
  `SPASynth Pro Library - Downloads.html`.
- Do NOT upload the 11 zips in the `Library/` subfolder (the current version
  folder likely has an empty `Library/` anyway, since library zips only live
  in the older 1.0.3/1.0.2 folders). The library is already on R2 at
  downloads.spasynth.com; the links file gives buyers the download URLs.

## Part 5: Create the Standard to Pro Upgrade

Repeat Parts 2 and 3 with:
- Title `SPASynth Standard to Pro Upgrade`; description = "3 · ... Upgrade".
- Price `400`, Compare-at `750`; URL handle `spasynth-standard-to-pro-upgrade`.
- Files from the current `SPASynth-Upgrade-<version>/` folder:
  `SPASynth Pro Library - Downloads.html` + `QUICKSTART.txt` + `EULA.txt`. No
  pkg/exe (upgraders already own the synth); no library upload (it is on R2).
- Honor system: Shopify cannot verify prior Standard ownership; fits the
  no-DRM stance.

## Part 6 (optional): free version for Everything Bundle owners

Business decision of record: Everything Bundle owners get SPASynth free. They
already own every sound, so they get the installer only and point it at their
existing library with "SET LIBRARY". The ready-to-send email is in
`docs/everything-bundle-email.md`.

Create the product (repeat Parts 2/3):
- Title `SPASynth for Everything Bundle Owners`.
- Files: pkg + exe + README + QUICKSTART + EULA only. NO library, NO links page.
- Price `99` (a notional value so a random link-finder does not get it free),
  publish to the Online Store, but do NOT add it to any collection or menu, so
  it is reachable by direct link only.

Create the 100%-off code:
1. Discounts -> Create discount -> Amount off products.
2. Method: Discount code. Type a code, e.g. `BUNDLEGIFT` (or Generate).
3. Value: Percentage, 100.
4. Applies to: Specific products -> select "SPASynth for Everything Bundle
   Owners" (so the code only zeroes out that one product).
5. Minimum requirements: none.
6. Maximum uses: cap total uses a little above your bundle-owner count; also
   "Limit to one use per customer".
7. Set a start date, Save.

Then email bundle owners the product link + the code (email is in
`docs/everything-bundle-email.md`). Simpler alternative: price the product `0`
and skip the code (a leaked free synth is useless without a library).

## Part 7: Tax and checkout

- Settings -> Taxes and duties: enable Shopify's handling for EU/UK VAT on
  digital goods if selling there. US is usually untaxed for digital, varies
  by state.
- No shipping settings needed (physical product is unchecked everywhere).

## Part 8: Test before launch (do not skip)

1. Temporarily set one product Active (or use test mode).
2. Buy it yourself; refund afterward.
3. Confirm: download email + links arrive; files download; the macOS
   installer opens with no Gatekeeper warning (signed + notarized); after
   install, SPASynth finds the Starter library and a preset plays.

## Part 9: Go live

1. Each product Status -> Active.
2. Ensure each is on the Online Store sales channel.
3. Add to a Collection if used.
