/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const tests = [];

for (let canonicalId of ["canonical", "canonical-001"]) {
  tests.push({
    locale: "en-US",
    region: "US",
    distribution: canonicalId,
    test: engines =>
      hasParams(engines, "Google", "client=ubuntu") &&
      hasParams(engines, "Google", "channel=fs") &&
      hasTelemetryId(engines, "Google", "google-canonical"),
  });

  tests.push({
    locale: "en-US",
    region: "GB",
    distribution: canonicalId,
    test: engines =>
      hasParams(engines, "Google", "client=ubuntu") &&
      hasParams(engines, "Google", "channel=fs") &&
      hasTelemetryId(engines, "Google", "google-canonical"),
  });
}

tests.push({
  locale: "en-US",
  region: "US",
  distribution: "canonical-002",
  test: engines =>
    hasParams(engines, "Google", "client=ubuntu-sn") &&
    hasParams(engines, "Google", "channel=fs") &&
    hasTelemetryId(engines, "Google", "google-ubuntu-sn"),
});

tests.push({
  locale: "en-US",
  region: "GB",
  distribution: "canonical-002",
  test: engines =>
    hasParams(engines, "Google", "client=ubuntu-sn") &&
    hasParams(engines, "Google", "channel=fs") &&
    hasTelemetryId(engines, "Google", "google-ubuntu-sn"),
});

tests.push({
  locale: "zh-CN",
  region: "CN",
  distribution: "MozillaOnline",
  test: engines =>
    hasEnginesFirst(engines, ["百度", "Bing", "Google", "维基百科"]),
});

tests.push({
  locale: "fr",
  distribution: "qwant-001",
  test: engines =>
    hasParams(engines, "Qwant", "client=firefoxqwant") &&
    hasDefault(engines, "Qwant") &&
    hasEnginesFirst(engines, ["Qwant", "Qwant Junior"]),
});

tests.push({
  locale: "fr",
  distribution: "qwant-001",
  test: engines => hasParams(engines, "Qwant Junior", "client=firefoxqwant"),
});

tests.push({
  locale: "fr",
  distribution: "qwant-002",
  test: engines =>
    hasParams(engines, "Qwant", "client=firefoxqwant") &&
    hasDefault(engines, "Qwant") &&
    hasEnginesFirst(engines, ["Qwant", "Qwant Junior"]),
});

tests.push({
  locale: "fr",
  distribution: "qwant-002",
  test: engines => hasParams(engines, "Qwant Junior", "client=firefoxqwant"),
});

for (const locale of ["en-US", "de"]) {
  tests.push({
    locale,
    distribution: "1und1",
    test: engines =>
      hasParams(engines, "1&1 Suche", "enc=UTF-8") &&
      hasDefault(engines, "1&1 Suche") &&
      hasEnginesFirst(engines, ["1&1 Suche"]),
  });

  tests.push({
    locale,
    distribution: "gmx",
    test: engines =>
      hasParams(engines, "GMX Suche", "enc=UTF-8") &&
      hasDefault(engines, "GMX Suche") &&
      hasEnginesFirst(engines, ["GMX Suche"]),
  });

  tests.push({
    locale,
    distribution: "gmx",
    test: engines => hasParams(engines, "GMX Shopping", "origin=br_osd"),
  });

  tests.push({
    locale,
    distribution: "mail.com",
    test: engines =>
      hasParams(engines, "mail.com search", "enc=UTF-8") &&
      hasDefault(engines, "mail.com search") &&
      hasEnginesFirst(engines, ["mail.com search"]),
  });

  tests.push({
    locale,
    distribution: "webde",
    test: engines =>
      hasParams(engines, "WEB.DE Suche", "enc=UTF-8") &&
      hasDefault(engines, "WEB.DE Suche") &&
      hasEnginesFirst(engines, ["WEB.DE Suche"]),
  });
}

tests.push({
  locale: "ru",
  region: "RU",
  distribution: "gmx",
  test: engines => hasDefault(engines, "GMX Suche"),
});

tests.push({
  locale: "en-GB",
  distribution: "gmxcouk",
  test: engines =>
    hasURLs(
      engines,
      "GMX Search",
      "https://go.gmx.co.uk/br/moz_search_web/?enc=UTF-8&q=test",
      "https://suggestplugin.gmx.co.uk/s?brand=gmxcouk&origin=moz_splugin_ff&enc=UTF-8&q=test"
    ) &&
    hasDefault(engines, "GMX Search") &&
    hasEnginesFirst(engines, ["GMX Search"]),
});

tests.push({
  locale: "ru",
  region: "RU",
  distribution: "gmxcouk",
  test: engines => hasDefault(engines, "GMX Search"),
});

tests.push({
  locale: "es",
  distribution: "gmxes",
  test: engines =>
    hasURLs(
      engines,
      "GMX - Búsqueda web",
      "https://go.gmx.es/br/moz_search_web/?enc=UTF-8&q=test",
      "https://suggestplugin.gmx.es/s?brand=gmxes&origin=moz_splugin_ff&enc=UTF-8&q=test"
    ) &&
    hasDefault(engines, "GMX - Búsqueda web") &&
    hasEnginesFirst(engines, ["GMX - Búsqueda web"]),
});

tests.push({
  locale: "ru",
  region: "RU",
  distribution: "gmxes",
  test: engines => hasDefault(engines, "GMX - Búsqueda web"),
});

tests.push({
  locale: "fr",
  distribution: "gmxfr",
  test: engines =>
    hasURLs(
      engines,
      "GMX - Recherche web",
      "https://go.gmx.fr/br/moz_search_web/?enc=UTF-8&q=test",
      "https://suggestplugin.gmx.fr/s?brand=gmxfr&origin=moz_splugin_ff&enc=UTF-8&q=test"
    ) &&
    hasDefault(engines, "GMX - Recherche web") &&
    hasEnginesFirst(engines, ["GMX - Recherche web"]),
});

tests.push({
  locale: "ru",
  region: "RU",
  distribution: "gmxfr",
  test: engines => hasDefault(engines, "GMX - Recherche web"),
});

tests.push({
  locale: "en-US",
  region: "US",
  distribution: "mint-001",
  test: engines =>
    hasParams(engines, "DuckDuckGo", "t=lm") &&
    hasParams(engines, "Google", "client=firefox-b-1-lm") &&
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-b-1-lm"),
});

tests.push({
  locale: "en-GB",
  region: "GB",
  distribution: "mint-001",
  test: engines =>
    hasParams(engines, "DuckDuckGo", "t=lm") &&
    hasParams(engines, "Google", "client=firefox-b-lm") &&
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-b-lm"),
});

tests.push({
  region: "ru",
  distribution: "mint-001",
  test: engines =>
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-com-nocodes"),
});

// This distribution is used on mobile, but we can test it here as it only
// needs the distribution id referencing.
tests.push({
  locale: "en-GB",
  region: "GB",
  distribution: "vivo-001",
  application: "firefox-android",
  test: engines =>
    hasParams(engines, "Bing", "pc=MZCP") &&
    hasParams(engines, "Bing", "form=MZVIVO") &&
    hasTelemetryId(engines, "Bing", "bing-MZCP") &&
    hasParams(engines, "Google", "client=firefox-b-vv") &&
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-b-vv"),
});

tests.push({
  region: "ru",
  distribution: "vivo-001",
  application: "firefox-android",
  test: engines =>
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-com-nocodes"),
});

// This distribution is used on mobile, but we can test it here as it only
// needs the distribution id referencing.
tests.push({
  locale: "en-GB",
  region: "GB",
  distribution: "dt-001",
  application: "firefox-android",
  test: engines =>
    hasParams(engines, "Google", "client=firefox-b-tf") &&
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-b-tf"),
});

// This distribution is used on mobile, but we can test it here as it only
// needs the distribution id referencing.
tests.push({
  locale: "en-GB",
  region: "GB",
  distribution: "dt-001",
  application: "firefox-android",
  test: engines =>
    hasParams(engines, "Bing", "pc=MZTOF") &&
    hasParams(engines, "Bing", "form=MZTOFO") &&
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Bing", "bing-MZTOF"),
});

tests.push({
  region: "ru",
  distribution: "dt-001",
  application: "firefox-android",
  test: engines =>
    hasDefault(engines, "Google") &&
    hasEnginesFirst(engines, ["Google"]) &&
    hasTelemetryId(engines, "Google", "google-com-nocodes"),
});

function hasURLs(engines, engineName, url, suggestURL) {
  let engine = engines.find(e => e.name === engineName);
  Assert.ok(engine, `Should be able to find ${engineName}`);

  let submission = engine.getSubmission("test", "text/html");
  Assert.equal(
    submission.uri.spec,
    url,
    `Should have the correct submission url for ${engineName}`
  );

  submission = engine.getSubmission("test", "application/x-suggestions+json");
  Assert.equal(
    submission.uri.spec,
    suggestURL,
    `Should have the correct suggestion url for ${engineName}`
  );
  return true;
}

function hasParams(engines, engineName, param) {
  let engine = engines.find(e => e.name === engineName);
  Assert.ok(engine, `Should be able to find ${engineName}`);
  let submission = engine.getSubmission("test", "text/html");
  let queries = submission.uri.query.split("&");

  let paramNames = new Set();
  for (let query of queries) {
    let queryParam = query.split("=")[0];
    Assert.ok(
      !paramNames.has(queryParam),
      `Should not have a duplicate ${queryParam} param`
    );
    paramNames.add(queryParam);
  }

  let result = queries.includes(param);
  Assert.ok(result, `expect ${submission.uri.query} to include ${param}`);
  return true;
}

function hasTelemetryId(engines, engineName, telemetryId) {
  let engine = engines.find(e => e.name === engineName);
  Assert.ok(engine, `Should be able to find ${engineName}`);

  Assert.equal(
    engine.telemetryId,
    telemetryId,
    "Should have the correct telemetryId"
  );
  return true;
}

function hasDefault(engines, expectedDefaultName) {
  Assert.equal(
    engines[0].name,
    expectedDefaultName,
    "Should have the expected engine set as default"
  );
  return true;
}

function hasEnginesFirst(engines, expectedEngines) {
  for (let [i, expectedEngine] of expectedEngines.entries()) {
    Assert.equal(
      engines[i].name,
      expectedEngine,
      `Should have the expected engine in position ${i}`
    );
  }
  return true;
}

add_setup(async function () {
  updateAppInfo({
    name: "firefox",
    ID: "xpcshell@tests.mozilla.org",
    version: "128",
    platformVersion: "128",
  });

  await maybeSetupConfig();
});

add_task(async function test_expected_distribution_engines() {
  let engineSelector = new SearchEngineSelector();

  for (const {
    application = "firefox",
    distribution,
    locale = "en-US",
    region = "US",
    test,
  } of tests) {
    let config = await engineSelector.fetchEngineConfiguration({
      locale,
      region,
      distroID: distribution,
      appName: application,
    });

    let engines = await SearchTestUtils.searchConfigToEngines(config.engines);
    engines = SearchUtils.sortEnginesByDefaults({
      engines,
      appDefaultEngine: engines[0],
    });
    test(engines);
  }
});
