/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test adding engines through the Address Bar context menu.

const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);
const BASE_URL =
  "http://mochi.test:8888/browser/browser/components/urlbar/tests/browser/";

add_task(async function context_none() {
  info("Checks the context menu with a page that doesn't offer any engines.");
  let url = "http://mochi.test:8888/";
  await BrowserTestUtils.withNewTab(url, async () => {
    await UrlbarTestUtils.withContextMenu(window, popup => {
      info("The separator and the add engine item should not be present.");
      let elt = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(!!elt);
      Assert.ok(!BrowserTestUtils.isVisible(elt));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-0"));
    });
  });
});

add_task(async function context_one() {
  info("Checks the context menu with a page that offers one engine.");
  let url = getRootDirectory(gTestPath) + "add_search_engine_one.html";
  await BrowserTestUtils.withNewTab(url, async () => {
    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine item should be present.");
      let elt = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(BrowserTestUtils.isVisible(elt));

      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-1"));

      elt = popup.parentNode.getMenuItem("add-engine-0");
      Assert.ok(BrowserTestUtils.isVisible(elt));
      await document.l10n.translateElements([elt]);
      Assert.ok(elt.label.includes("add_search_engine_0"));
      Assert.ok(elt.hasAttribute("image"));
      Assert.equal(
        elt.getAttribute("uri"),
        BASE_URL + "add_search_engine_0.xml"
      );

      info("Click on the menuitem");
      let enginePromise = SearchTestUtils.promiseEngine("add_search_engine_0");
      popup.activateItem(elt);
      await enginePromise;
      Assert.equal(popup.state, "closed");
    });

    await UrlbarTestUtils.withContextMenu(window, popup => {
      info("The separator and the add engine item should not be present.");
      let elt = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(!BrowserTestUtils.isVisible(elt));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-0"));
    });

    info("Remove the engine.");
    let engine = await Services.search.getEngineByName("add_search_engine_0");
    await Services.search.removeEngine(engine);

    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine item should be present again.");
      let elt = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(BrowserTestUtils.isVisible(elt));

      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-1"));

      elt = popup.parentNode.getMenuItem("add-engine-0");
      Assert.ok(BrowserTestUtils.isVisible(elt));
      await document.l10n.translateElements([elt]);
      Assert.ok(elt.label.includes("add_search_engine_0"));
    });
  });
});

add_task(async function context_invalid() {
  info("Checks the context menu with a page that offers an invalid engine.");

  let url = getRootDirectory(gTestPath) + "add_search_engine_invalid.html";
  await BrowserTestUtils.withNewTab(url, async tab => {
    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine item should be present.");
      Assert.ok(popup.parentNode.getMenuItem("add-engine-separator"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-1"));

      let elt = popup.parentNode.getMenuItem("add-engine-0");
      Assert.ok(BrowserTestUtils.isVisible(elt));
      await document.l10n.translateElements([elt]);
      Assert.ok(elt.label.includes("add_search_engine_404"));
      Assert.equal(
        elt.getAttribute("uri"),
        BASE_URL + "add_search_engine_404.xml"
      );

      info("Click on the menuitem");
      let promptPromise = PromptTestUtils.waitForPrompt(tab.linkedBrowser, {
        modalType: Ci.nsIPromptService.MODAL_TYPE_CONTENT,
        promptType: "alert",
      });

      popup.activateItem(elt);

      let prompt = await promptPromise;
      Assert.ok(
        prompt.ui.infoBody.textContent.includes(
          BASE_URL + "add_search_engine_404.xml"
        ),
        "Should have included the url in the prompt body"
      );
      await PromptTestUtils.handlePrompt(prompt);
      Assert.equal(popup.state, "closed");
    });
  });
});

add_task(async function context_same_name() {
  info("Checks the context menu with a page that offers same named engines.");
  let url = getRootDirectory(gTestPath) + "add_search_engine_same_names.html";
  await BrowserTestUtils.withNewTab(url, async () => {
    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine item should be present.");
      let elt = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(BrowserTestUtils.isVisible(elt));

      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-1"));

      elt = popup.parentNode.getMenuItem("add-engine-0");
      Assert.ok(BrowserTestUtils.isVisible(elt));
      await document.l10n.translateElements([elt]);
      Assert.ok(elt.label.includes("add_search_engine_0"));
    });
  });
});

add_task(async function context_two() {
  info("Checks the context menu with a page that offers two engines.");
  let url = getRootDirectory(gTestPath) + "add_search_engine_two.html";
  await BrowserTestUtils.withNewTab(url, async () => {
    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine item should be present.");
      let elt = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(BrowserTestUtils.isVisible(elt));

      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));

      elt = popup.parentNode.getMenuItem("add-engine-0");
      Assert.ok(BrowserTestUtils.isVisible(elt));
      await document.l10n.translateElements([elt]);
      Assert.ok(elt.label.includes("add_search_engine_0"));
      elt = popup.parentNode.getMenuItem("add-engine-1");
      Assert.ok(BrowserTestUtils.isVisible(elt));
      await document.l10n.translateElements([elt]);
      Assert.ok(elt.label.includes("add_search_engine_1"));
    });
  });
});

add_task(async function context_many() {
  info("Checks the context menu with a page that offers many engines.");
  let url = getRootDirectory(gTestPath) + "add_search_engine_many.html";
  await BrowserTestUtils.withNewTab(url, async () => {
    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine menu should be present.");
      let separator = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(BrowserTestUtils.isVisible(separator));

      info("Engines should appear in sub menu");
      let menu = popup.parentNode.getMenuItem("add-engine-menu");
      Assert.ok(BrowserTestUtils.isVisible(menu));
      Assert.ok(
        !menu.nextElementSibling
          ?.getAttribute("anonid")
          .startsWith("add-engine")
      );
      Assert.ok(menu.hasAttribute("image"), "Menu should have an icon");
      Assert.ok(
        !menu.label.includes("add-engine"),
        "Menu should not contain an engine name"
      );

      info("Open the submenu");
      let popupShown = BrowserTestUtils.waitForEvent(menu, "popupshown");
      menu.openMenu(true);
      await popupShown;
      for (let i = 0; i < 4; ++i) {
        let elt = popup.parentNode.getMenuItem(`add-engine-${i}`);
        Assert.equal(elt.parentNode, menu.menupopup);
        Assert.ok(BrowserTestUtils.isVisible(elt));
      }

      info("Click on the first engine to install it");
      let enginePromise = SearchTestUtils.promiseEngine("add_search_engine_0");
      let elt = popup.parentNode.getMenuItem("add-engine-0");

      elt.closest("menupopup").activateItem(elt);
      await enginePromise;
      Assert.equal(popup.state, "closed");
    });

    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("Check the installed engine has been removed");
      // We're below the limit of engines for the menu now.
      Assert.ok(!!popup.parentNode.getMenuItem("add-engine-separator"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));

      for (let i = 0; i < 3; ++i) {
        let elt = popup.parentNode.getMenuItem(`add-engine-${i}`);
        Assert.equal(elt.parentNode, popup);
        Assert.ok(BrowserTestUtils.isVisible(elt));
        await document.l10n.translateElements([elt]);
        Assert.ok(elt.label.includes(`add_search_engine_${i + 1}`));
      }
    });

    info("Remove the engine.");
    let engine = await Services.search.getEngineByName("add_search_engine_0");
    await Services.search.removeEngine(engine);

    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine menu should be present.");
      let separator = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(BrowserTestUtils.isVisible(separator));

      info("Engines should appear in sub menu");
      let menu = popup.parentNode.getMenuItem("add-engine-menu");
      Assert.ok(BrowserTestUtils.isVisible(menu));
      Assert.ok(
        !menu.nextElementSibling
          ?.getAttribute("anonid")
          .startsWith("add-engine")
      );

      info("Open the submenu");
      let popupShown = BrowserTestUtils.waitForEvent(menu, "popupshown");
      menu.openMenu(true);
      await popupShown;
      for (let i = 0; i < 4; ++i) {
        let elt = popup.parentNode.getMenuItem(`add-engine-${i}`);
        Assert.equal(elt.parentNode, menu.menupopup);
        if (
          AppConstants.platform != "macosx" ||
          !Services.prefs.getBoolPref(
            "widget.macos.native-context-menus",
            false
          )
        ) {
          Assert.ok(BrowserTestUtils.isVisible(elt));
        }
      }
    });
  });
});

add_task(async function context_after_customize() {
  info("Checks the context menu after customization.");
  let url = getRootDirectory(gTestPath) + "add_search_engine_one.html";
  await BrowserTestUtils.withNewTab(url, async () => {
    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine item should be present.");
      let elt = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(BrowserTestUtils.isVisible(elt));

      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-1"));

      elt = popup.parentNode.getMenuItem("add-engine-0");
      Assert.ok(BrowserTestUtils.isVisible(elt));
      await document.l10n.translateElements([elt]);
      Assert.ok(elt.label.includes("add_search_engine_0"));
    });

    let promise = BrowserTestUtils.waitForEvent(
      gNavToolbox,
      "customizationready"
    );
    gCustomizeMode.enter();
    await promise;
    promise = BrowserTestUtils.waitForEvent(gNavToolbox, "aftercustomization");
    gCustomizeMode.exit();
    await promise;

    // Wait for the urlbar to pop out again before trying to show the context
    // menu. Otherwise the reframing might hide the context menu (this is a
    // long-standing XUL issue).
    await TestUtils.waitForCondition(() => {
      return window.gURLBar.textbox.hasAttribute("breakout");
    });

    await UrlbarTestUtils.withContextMenu(window, async popup => {
      info("The separator and the add engine item should be present.");
      let elt = popup.parentNode.getMenuItem("add-engine-separator");
      Assert.ok(BrowserTestUtils.isVisible(elt));

      Assert.ok(!popup.parentNode.getMenuItem("add-engine-menu"));
      Assert.ok(!popup.parentNode.getMenuItem("add-engine-1"));

      elt = popup.parentNode.getMenuItem("add-engine-0");
      Assert.ok(BrowserTestUtils.isVisible(elt));
      await document.l10n.translateElements([elt]);
      Assert.ok(elt.label.includes("add_search_engine_0"));
    });
  });
});
