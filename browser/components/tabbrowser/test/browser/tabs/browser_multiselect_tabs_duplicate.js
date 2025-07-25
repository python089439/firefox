async function openTabMenuFor(tab) {
  let tabMenu = tab.ownerDocument.getElementById("tabContextMenu");

  let tabMenuShown = BrowserTestUtils.waitForEvent(tabMenu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    tab,
    { type: "contextmenu" },
    tab.ownerGlobal
  );
  await tabMenuShown;

  return tabMenu;
}

add_task(async function test() {
  let originalTab = gBrowser.selectedTab;
  // eslint-disable-next-line @microsoft/sdl/no-insecure-url
  let tab1 = await addTab("http://example.com/1");
  // eslint-disable-next-line @microsoft/sdl/no-insecure-url
  let tab2 = await addTab("http://example.com/2");
  // eslint-disable-next-line @microsoft/sdl/no-insecure-url
  let tab3 = await addTab("http://example.com/3");

  let menuItemDuplicateTab = document.getElementById("context_duplicateTab");
  let menuItemDuplicateTabs = document.getElementById("context_duplicateTabs");

  is(gBrowser.multiSelectedTabsCount, 0, "Zero multiselected tabs");

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await triggerClickOn(tab2, { ctrlKey: true });

  ok(tab1.multiselected, "Tab1 is multiselected");
  ok(tab2.multiselected, "Tab2 is multiselected");
  ok(!tab3.multiselected, "Tab3 is not multiselected");

  // Check the context menu with a multiselected tabs
  updateTabContextMenu(tab2);
  is(menuItemDuplicateTab.hidden, true, "Duplicate Tab is hidden");
  is(menuItemDuplicateTabs.hidden, false, "Duplicate Tabs is visible");

  // Check the context menu with a non-multiselected tab
  updateTabContextMenu(tab3);
  is(menuItemDuplicateTab.hidden, false, "Duplicate Tab is visible");
  is(menuItemDuplicateTabs.hidden, true, "Duplicate Tabs is hidden");

  let newTabOpened = BrowserTestUtils.waitForNewTab(
    gBrowser,
    // eslint-disable-next-line @microsoft/sdl/no-insecure-url
    "http://example.com/3",
    true
  );
  {
    let menu = await openTabMenuFor(tab3);
    menu.activateItem(menuItemDuplicateTab);
  }
  let tab4 = await newTabOpened;

  is(
    getUrl(tab4),
    getUrl(tab3),
    "tab4 should have same URL as tab3, where it was duplicated from"
  );

  // Selection should be cleared after duplication
  ok(!tab1.multiselected, "Tab1 is not multiselected");
  ok(!tab2.multiselected, "Tab2 is not multiselected");
  ok(!tab3.multiselected, "Tab3 is not multiselected");
  ok(!tab4.multiselected, "Tab4 is not multiselected");

  is(gBrowser.selectedTab._tPos, tab4._tPos, "Tab4 should be selected");

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await triggerClickOn(tab3, { ctrlKey: true });

  ok(tab1.multiselected, "Tab1 is multiselected");
  ok(!tab2.multiselected, "Tab2 is not multiselected");
  ok(tab3.multiselected, "Tab3 is multiselected");
  ok(!tab4.multiselected, "Tab4 is not multiselected");

  // Check the context menu with a non-multiselected tab
  updateTabContextMenu(tab3);
  is(menuItemDuplicateTab.hidden, true, "Duplicate Tab is hidden");
  is(menuItemDuplicateTabs.hidden, false, "Duplicate Tabs is visible");

  // 7 tabs because there was already one open when the test starts.
  // Can't use BrowserTestUtils.waitForNewTab because waitForNewTab only works
  // with one tab at a time.
  let newTabsOpened = TestUtils.waitForCondition(
    () => gBrowser.visibleTabs.length == 7,
    "Wait for two tabs to get created"
  );
  {
    let menu = await openTabMenuFor(tab3);
    menu.activateItem(menuItemDuplicateTabs);
  }
  await newTabsOpened;
  info("Two tabs opened");

  await TestUtils.waitForCondition(() => {
    return (
      // eslint-disable-next-line @microsoft/sdl/no-insecure-url
      getUrl(gBrowser.visibleTabs[4]) == "http://example.com/1" &&
      // eslint-disable-next-line @microsoft/sdl/no-insecure-url
      getUrl(gBrowser.visibleTabs[5]) == "http://example.com/3"
    );
  });

  is(
    originalTab,
    gBrowser.visibleTabs[0],
    "Original tab should still be first"
  );
  is(tab1, gBrowser.visibleTabs[1], "tab1 should still be second");
  is(tab2, gBrowser.visibleTabs[2], "tab2 should still be third");
  is(tab3, gBrowser.visibleTabs[3], "tab3 should still be fourth");
  is(
    getUrl(gBrowser.visibleTabs[4]),
    getUrl(tab1),
    "the first duplicated tab should be placed next to tab3 and have URL of tab1"
  );
  is(
    getUrl(gBrowser.visibleTabs[5]),
    getUrl(tab3),
    "the second duplicated tab should have URL of tab3 and maintain same order"
  );
  is(
    tab4,
    gBrowser.visibleTabs[6],
    "tab4 should now be the still be the seventh tab"
  );

  let group = gBrowser.addTabGroup([
    gBrowser.visibleTabs[1],
    gBrowser.visibleTabs[2],
    gBrowser.visibleTabs[3],
  ]);
  gBrowser.selectedTabs = group.tabs;
  let menu = await openTabMenuFor(group.tabs[0]);
  menu.activateItem(menuItemDuplicateTabs);
  // Can't use BrowserTestUtils.waitForNewTab because waitForNewTab only works
  // with one tab at a time.
  await TestUtils.waitForCondition(
    () => gBrowser.visibleTabs.length == 10,
    "Wait for three tabs to get created"
  );
  is(group.tabs.length, 6, "All tabs were duplicated into the tab group");

  let tabsToClose = gBrowser.visibleTabs.filter(t => t != originalTab);
  for (let tab of tabsToClose) {
    BrowserTestUtils.removeTab(tab);
  }
});
