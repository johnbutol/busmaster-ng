// BUSMASTER-NG site: latest release and download counter, read from the GitHub API (no account, no tracking).
// The answer is kept for 10 minutes in sessionStorage: the API allows 60 requests per hour per visitor.
(function () {
  var API = "https://api.github.com/repos/teoCLMR/busmaster-ng/releases?per_page=100";
  var CACHE_KEY = "bmng-releases-2";
  var CACHE_MS = 10 * 60 * 1000;
  var DIGITS = 6;

  function cached() {
    try {
      var c = JSON.parse(sessionStorage.getItem(CACHE_KEY));
      if (c && Date.now() - c.time < CACHE_MS) return c.data;
    } catch (e) { }
    return null;
  }

  function store(data) {
    try { sessionStorage.setItem(CACHE_KEY, JSON.stringify({ time: Date.now(), data: data })); } catch (e) { }
  }

  function load() {
    var c = cached();
    if (c) return Promise.resolve(c);
    return fetch(API, { headers: { Accept: "application/vnd.github+json" } }).then(function (r) {
      if (!r.ok) throw new Error("GitHub API " + r.status);
      return r.json();
    }).then(function (data) {
      // keep only what the page needs
      var slim = data.filter(function (rel) { return !rel.draft; }).map(function (rel) {
        return {
          name: rel.name || rel.tag_name, tag: rel.tag_name, url: rel.html_url,
          date: rel.published_at, prerelease: rel.prerelease,
          assets: rel.assets.filter(function (a) { return /\.(zip|exe)$/i.test(a.name); }).map(function (a) {
            return { name: a.name, url: a.browser_download_url, size: a.size, count: a.download_count };
          })
        };
      });
      store(slim);
      return slim;
    });
  }

  function showCounter(total) {
    var box = document.getElementById("counter");
    var text = String(total);
    while (text.length < DIGITS) text = " " + text;
    box.innerHTML = "";
    for (var i = 0; i < text.length; i++) {
      var d = document.createElement("span");
      if (text[i] === " ") { d.textContent = "0"; d.className = "off"; }
      else d.textContent = text[i];
      box.appendChild(d);
    }
    box.setAttribute("aria-label", total + " downloads");
  }

  // From 3.4.2 a release has an installer and two portable zips (Windows 10/11 and Windows 7); older releases
  // have a single zip.
  function findAsset(rel, re) {
    return rel.assets.filter(function (a) { return re.test(a.name); })[0];
  }

  function mb(asset) {
    return (asset.size / 1048576).toFixed(1) + " MB";
  }

  function showPortable(rel) {
    var zips = [
      { asset: findAsset(rel, /_win32\.zip$/i), label: "Windows 10 / 11" },
      { asset: findAsset(rel, /_win7\.zip$/i), label: "Windows 7 SP1" }
    ].filter(function (z) { return z.asset; });
    var el = document.getElementById("portable");
    if (!zips.length || !findAsset(rel, /\.exe$/i)) return;
    el.innerHTML = "";
    el.appendChild(document.createTextNode("Portable zip, no setup: "));
    zips.forEach(function (z, i) {
      if (i) el.appendChild(document.createTextNode(" · "));
      var a = document.createElement("a");
      a.href = z.asset.url;
      a.textContent = z.label + " (" + mb(z.asset) + ")";
      el.appendChild(a);
    });
  }

  function showRelease(rel) {
    var asset = findAsset(rel, /\.exe$/i) || findAsset(rel, /_win32\.zip$/i) || rel.assets[0];
    var date = new Date(rel.date).toLocaleDateString("en-GB", { day: "numeric", month: "long", year: "numeric" });
    var el = document.getElementById("release");
    el.innerHTML = "";
    el.appendChild(document.createTextNode("Latest release: "));
    var a = document.createElement("a");
    a.href = rel.url;
    a.textContent = rel.name;
    el.appendChild(a);
    var d = document.createElement("span");
    d.className = "date";
    d.textContent = " · " + date;
    el.appendChild(d);
    if (asset) {
      var what = /\.exe$/i.test(asset.name) ? "Download installer " : "Download ";
      document.getElementById("download-button").href = asset.url;
      document.getElementById("download-button").textContent = what + rel.tag + " (" + mb(asset) + ")";
      document.getElementById("hero-download").href = asset.url;
      document.getElementById("hero-download").textContent = what + rel.tag;
    }
    showPortable(rel);
  }

  // FAQPage structured data for search engines, built from the questions on the page so the two never differ
  function addFaqData() {
    var items = [].map.call(document.querySelectorAll("#faq details"), function (d) {
      return {
        "@type": "Question", name: d.querySelector("summary").textContent.trim(),
        acceptedAnswer: { "@type": "Answer", text: d.querySelector("p").textContent.replace(/\s+/g, " ").trim() }
      };
    });
    if (!items.length) return;
    var s = document.createElement("script");
    s.type = "application/ld+json";
    s.textContent = JSON.stringify({ "@context": "https://schema.org", "@type": "FAQPage", mainEntity: items });
    document.head.appendChild(s);
  }

  addFaqData();

  load().then(function (releases) {
    var total = 0;
    releases.forEach(function (rel) {
      rel.assets.forEach(function (a) { total += a.count; });
    });
    showCounter(total);
    var latest = releases.filter(function (r) { return !r.prerelease; })[0];
    if (latest) showRelease(latest);
  }).catch(function () {
    // GitHub not reachable or rate limit reached: the static links to the Releases page stay
    document.getElementById("counter-note").textContent = "not available right now";
  });
})();
