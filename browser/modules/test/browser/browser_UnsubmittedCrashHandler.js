"use strict";

/**
 * This suite tests the "unsubmitted crash report" notification
 * that is seen when we detect pending crash reports on startup.
 */

const { UnsubmittedCrashHandler } = ChromeUtils.importESModule(
  "resource:///modules/ContentCrashHandlers.sys.mjs"
);

const { makeFakeAppDir } = ChromeUtils.importESModule(
  "resource://testing-common/AppData.sys.mjs"
);

const DAY = 24 * 60 * 60 * 1000; // milliseconds
const SERVER_URL =
  "http://example.com/browser/toolkit/crashreporter/test/browser/crashreport.sjs";

/**
 * Returns the directly where the browsing is storing the
 * pending crash reports.
 *
 * @returns nsIFile
 */
function getPendingCrashReportDir() {
  // The fake UAppData directory that makeFakeAppDir provides
  // is just UAppData under the profile directory.
  return FileUtils.getDir("ProfD", ["UAppData", "Crash Reports", "pending"]);
}

/**
 * Synchronously deletes all entries inside the pending
 * crash report directory.
 */
async function clearPendingCrashReports() {
  let dir = getPendingCrashReportDir();
  let entries = dir.directoryEntries;

  while (entries.hasMoreElements()) {
    let entry = entries.nextFile;
    if (entry.isFile()) {
      await entry.remove(false);
    }
  }
}

/**
 * Randomly generates howMany crash report .dmp and .extra files
 * to put into the pending crash report directory. We're not
 * actually creating real crash reports here, just stubbing
 * out enough of the files to satisfy our notification and
 * submission code.
 *
 * @param howMany (int)
 *        How many pending crash reports to put in the pending
 *        crash report directory.
 * @param accessDate (Date, optional)
 *        What date to set as the last accessed time on the created
 *        crash reports. This defaults to the current date and time.
 * @returns Promise
 */
function createPendingCrashReports(howMany, accessDate) {
  let dir = getPendingCrashReportDir();
  if (!accessDate) {
    accessDate = new Date();
  }

  /**
   * Helper function for creating a file in the pending crash report
   * directory.
   *
   * @param fileName (string)
   *        The filename for the crash report, not including the
   *        extension. This is usually a UUID.
   * @param extension (string)
   *        The file extension for the created file.
   * @param accessDate (Date, optional)
   *        The date to set lastAccessed to, if anything.
   * @param contents (string, optional)
   *        Set this to whatever the file needs to contain, if anything.
   * @returns Promise
   */
  let createFile = async (fileName, extension, lastAccessedDate, contents) => {
    let file = dir.clone();
    file.append(fileName + "." + extension);
    file.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);

    if (contents) {
      await IOUtils.writeUTF8(file.path, contents, {
        tmpPath: file.path + ".tmp",
      });
    }

    if (lastAccessedDate) {
      await IOUtils.setAccessTime(file.path, lastAccessedDate.valueOf());
    }
  };

  let uuidGenerator = Services.uuid;
  // Some annotations are always present in the .extra file and CrashSubmit.sys.mjs
  // expects there to be a ServerURL entry, so we'll add them here.
  let extraFileContents = JSON.stringify({
    ServerURL: SERVER_URL,
    TelemetryServerURL: "http://telemetry.mozilla.org/",
    TelemetryClientId: "c69e7487-df10-4c98-ab1a-c85660feecf3",
    TelemetrySessionId: "22af5a41-6e84-4112-b1f7-4cb12cb6f6a5",
  });

  return (async function () {
    let uuids = [];
    for (let i = 0; i < howMany; ++i) {
      let uuid = uuidGenerator.generateUUID().toString();
      // Strip the {}...
      uuid = uuid.substring(1, uuid.length - 1);
      await createFile(uuid, "dmp", accessDate);
      await createFile(uuid, "extra", accessDate, extraFileContents);
      uuids.push(uuid);
    }
    return uuids;
  })();
}

/**
 * Returns a Promise that resolves once CrashSubmit starts sending
 * success notifications for crash submission matching the reportIDs
 * being passed in.
 *
 * @param reportIDs (Array<string>)
 *        The IDs for the reports that we expect CrashSubmit to have sent.
 * @param extraCheck (Function, optional)
 *        A function that receives the annotations of the crash report and can
 *        be used for checking them
 * @returns Promise
 */
function waitForSubmittedReports(reportIDs, extraCheck) {
  let promises = [];
  for (let reportID of reportIDs) {
    let promise = TestUtils.topicObserved(
      "crash-report-status",
      (subject, data) => {
        if (data == "success") {
          let propBag = subject.QueryInterface(Ci.nsIPropertyBag2);
          let dumpID = propBag.getPropertyAsAString("minidumpID");
          if (dumpID == reportID) {
            if (extraCheck) {
              let extra = propBag.getPropertyAsInterface(
                "extra",
                Ci.nsIPropertyBag2
              );

              extraCheck(extra);
            }

            return true;
          }
        }
        return false;
      }
    );
    promises.push(promise);
  }
  return Promise.all(promises);
}

/**
 * Returns a Promise that resolves once a .dmp.ignore file is created for
 * the crashes in the pending directory matching the reportIDs being
 * passed in.
 *
 * @param reportIDs (Array<string>)
 *        The IDs for the reports that we expect CrashSubmit to have been
 *        marked for ignoring.
 * @returns Promise
 */
function waitForIgnoredReports(reportIDs) {
  let dir = getPendingCrashReportDir();
  let promises = [];
  for (let reportID of reportIDs) {
    let file = dir.clone();
    file.append(reportID + ".dmp.ignore");
    promises.push(IOUtils.exists(file.path));
  }
  return Promise.all(promises);
}

add_setup(async function () {
  // Pending crash reports are stored in the UAppData folder,
  // which exists outside of the profile folder. In order to
  // not overwrite / clear pending crash reports for the poor
  // soul who runs this test, we use AppData.sys.mjs to point to
  // a special made-up directory inside the profile
  // directory.
  await makeFakeAppDir();
  // We'll assume that the notifications will be shown in the current
  // browser window's global notification box.

  // If we happen to already be seeing the unsent crash report
  // notification, it's because the developer running this test
  // happened to have some unsent reports in their UAppDir.
  // We'll remove the notification without touching those reports.
  let notification = gNotificationBox.getNotificationWithValue(
    "pending-crash-reports"
  );
  if (notification) {
    notification.close();
  }

  let oldServerURL = Services.env.get("MOZ_CRASHREPORTER_URL");
  Services.env.set("MOZ_CRASHREPORTER_URL", SERVER_URL);

  // nsBrowserGlue starts up UnsubmittedCrashHandler automatically
  // on a timer, so at this point, it can be in one of several states:
  //
  // 1. The timer hasn't yet finished, and an automatic scan for crash
  //    reports is pending.
  // 2. The timer has already gone off and the scan has already completed.
  // 3. The handler is disabled.
  //
  // To collapse all of these possibilities, we uninit the UnsubmittedCrashHandler
  // to cancel the timer, make sure it's preffed on, and then restart it (which
  // doesn't restart the timer). Note that making the component initialize
  // even when it's disabled is an intentional choice, as this allows for easier
  // simulation of startup and shutdown.
  UnsubmittedCrashHandler.uninit();

  // While we're here, let's test that we don't show the notification
  // if we're disabled and something happens to check for unsubmitted
  // crash reports.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.crashReports.unsubmittedCheck.enabled", false]],
  });

  await createPendingCrashReports(1);

  notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(!notification, "There should not be a notification");

  await clearPendingCrashReports();
  await SpecialPowers.popPrefEnv();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.crashReports.unsubmittedCheck.enabled", true]],
  });
  UnsubmittedCrashHandler.init();

  registerCleanupFunction(async function () {
    await clearPendingCrashReports();
    Services.env.set("MOZ_CRASHREPORTER_URL", oldServerURL);
  });
});

/**
 * Tests that if there are no pending crash reports, then the
 * notification will not show up.
 */
add_task(async function test_no_pending_no_notification() {
  // Make absolutely sure there are no pending crash reports first...
  await clearPendingCrashReports();
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.equal(
    notification,
    null,
    "There should not be a notification if there are no " +
      "pending crash reports"
  );
});

/**
 * Tests that there is a notification if there is one pending
 * crash report.
 */
add_task(async function test_one_pending() {
  await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");
  gNotificationBox.removeNotification(notification, true);
  await clearPendingCrashReports();
});

/**
 * Tests that an ignored crash report does not suppress a notification that
 * would be trigged by another, unignored crash report.
 */
add_task(async function test_other_ignored() {
  let toIgnore = await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  // Dismiss notification, creating the .dmp.ignore file
  notification.closeButton.click();
  gNotificationBox.removeNotification(notification, true);
  await waitForIgnoredReports(toIgnore);

  notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(!notification, "There should not be a notification");

  await createPendingCrashReports(1);
  notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  gNotificationBox.removeNotification(notification, true);
  await clearPendingCrashReports();
});

/**
 * Tests that there is a notification if there is more than one
 * pending crash report.
 */
add_task(async function test_several_pending() {
  await createPendingCrashReports(3);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  gNotificationBox.removeNotification(notification, true);
  await clearPendingCrashReports();
});

/**
 * Tests that there is no notification if the only pending crash
 * reports are over 28 days old. Also checks that if we put a newer
 * crash with that older set, that we can still get a notification.
 */
add_task(async function test_several_pending() {
  // Let's create some crash reports from 30 days ago.
  let oldDate = new Date(Date.now() - 30 * DAY);
  await createPendingCrashReports(3, oldDate);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.equal(
    notification,
    null,
    "There should not be a notification if there are only " +
      "old pending crash reports"
  );
  // Now let's create a new one and check again
  await createPendingCrashReports(1);
  notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  gNotificationBox.removeNotification(notification, true);
  await clearPendingCrashReports();
});

/**
 * Tests that the notification can submit a report.
 */
add_task(async function test_can_submit() {
  function extraCheck(extra) {
    const blockedAnnotations = [
      "ServerURL",
      "TelemetryClientId",
      "TelemetryServerURL",
      "TelemetrySessionId",
    ];
    for (const key of blockedAnnotations) {
      Assert.ok(
        !extra.hasKey(key),
        "The " + key + " annotation should have been stripped away"
      );
    }

    Assert.equal(extra.get("SubmittedFrom"), "Infobar");
    Assert.equal(extra.get("Throttleable"), "1");
  }

  let reportIDs = await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  // Attempt to submit the notification by clicking on the submit
  // button
  let buttons = notification.buttonContainer.querySelectorAll(
    ".notification-button"
  );
  // ...which should be the first button.
  let submit = buttons[0];
  let submissionBefore = Glean.crashSubmission.success.testGetValue();

  let promiseReports = waitForSubmittedReports(reportIDs, extraCheck);
  info("Sending crash report");
  submit.click();
  info("Sent!");
  // We'll not wait for the notification to finish its transition -
  // we'll just remove it right away.
  gNotificationBox.removeNotification(notification, true);

  info("Waiting on reports to be received.");
  await promiseReports;
  info("Received!");

  Assert.equal(
    submissionBefore + 1,
    Glean.crashSubmission.success.testGetValue()
  );
  await clearPendingCrashReports();
});

/**
 * Tests that the notification can submit multiple reports.
 */
add_task(async function test_can_submit_several() {
  let reportIDs = await createPendingCrashReports(3);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  // Attempt to submit the notification by clicking on the submit
  // button
  let buttons = notification.buttonContainer.querySelectorAll(
    ".notification-button"
  );
  // ...which should be the first button.
  let submit = buttons[0];

  let submissionBefore = Glean.crashSubmission.success.testGetValue();

  let promiseReports = waitForSubmittedReports(reportIDs);
  info("Sending crash reports");
  submit.click();
  info("Sent!");
  // We'll not wait for the notification to finish its transition -
  // we'll just remove it right away.
  gNotificationBox.removeNotification(notification, true);

  info("Waiting on reports to be received.");
  await promiseReports;
  info("Received!");

  Assert.equal(
    submissionBefore + 3,
    Glean.crashSubmission.success.testGetValue()
  );
  await clearPendingCrashReports();
});

/**
 * Tests that choosing "Send Always" flips the autoSubmit pref
 * and sends the pending crash reports.
 */
add_task(async function test_can_submit_always() {
  let pref = "browser.crashReports.unsubmittedCheck.autoSubmit2";
  Assert.equal(
    Services.prefs.getBoolPref(pref),
    false,
    "We should not be auto-submitting by default"
  );

  let reportIDs = await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  // Attempt to submit the notification by clicking on the send all
  // button
  let buttons = notification.buttonContainer.querySelectorAll(
    ".notification-button"
  );
  // ...which should be the second button.
  let sendAll = buttons[1];
  let submissionBefore = Glean.crashSubmission.success.testGetValue();

  let promiseReports = waitForSubmittedReports(reportIDs);
  info("Sending crash reports");
  sendAll.click();
  info("Sent!");
  // We'll not wait for the notification to finish its transition -
  // we'll just remove it right away.
  gNotificationBox.removeNotification(notification, true);

  info("Waiting on reports to be received.");
  await promiseReports;
  info("Received!");
  Assert.equal(
    submissionBefore + 1,
    Glean.crashSubmission.success.testGetValue()
  );

  // Make sure the pref was set
  Assert.equal(
    Services.prefs.getBoolPref(pref),
    true,
    "The autoSubmit pref should have been set"
  );

  // Create another report
  reportIDs = await createPendingCrashReports(1);
  let result = await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();

  // Check that the crash was auto-submitted
  Assert.equal(result, null, "The notification should not be shown");
  promiseReports = await waitForSubmittedReports(reportIDs, extra => {
    Assert.equal(extra.get("SubmittedFrom"), "Auto");
    Assert.equal(extra.get("Throttleable"), "1");
  });

  Assert.equal(
    submissionBefore + 2,
    Glean.crashSubmission.success.testGetValue()
  );

  // And revert back to default now.
  Services.prefs.clearUserPref(pref);

  await clearPendingCrashReports();
});

/**
 * Tests that if the user has chosen to automatically send
 * crash reports that no notification is displayed to the
 * user.
 */
add_task(async function test_can_auto_submit() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.crashReports.unsubmittedCheck.autoSubmit2", true]],
  });

  let reportIDs = await createPendingCrashReports(3);
  let submissionBefore = Glean.crashSubmission.success.testGetValue();
  let promiseReports = waitForSubmittedReports(reportIDs);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.equal(notification, null, "There should be no notification");
  info("Waiting on reports to be received.");
  await promiseReports;
  info("Received!");
  Assert.equal(
    submissionBefore + 3,
    Glean.crashSubmission.success.testGetValue()
  );

  await clearPendingCrashReports();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that if the user chooses to dismiss the notification,
 * then the current pending requests won't cause the notification
 * to appear again in the future.
 */
add_task(async function test_can_ignore() {
  let reportIDs = await createPendingCrashReports(3);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  // Dismiss the notification by clicking on the "X" button.
  notification.closeButton.click();
  // We'll not wait for the notification to finish its transition -
  // we'll just remove it right away.
  gNotificationBox.removeNotification(notification, true);
  await waitForIgnoredReports(reportIDs);

  notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.equal(notification, null, "There should be no notification");

  await clearPendingCrashReports();
});

/**
 * Tests that if the notification is shown, then the
 * lastShownDate is set for today.
 */
add_task(async function test_last_shown_date() {
  await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  let today = UnsubmittedCrashHandler.dateString(new Date());
  let lastShownDate =
    UnsubmittedCrashHandler.prefs.getCharPref("lastShownDate");
  Assert.equal(today, lastShownDate, "Last shown date should be today.");

  UnsubmittedCrashHandler.prefs.clearUserPref("lastShownDate");
  gNotificationBox.removeNotification(notification, true);
  await clearPendingCrashReports();
});

/**
 * Tests that if UnsubmittedCrashHandler is uninit with a
 * notification still being shown, that
 * browser.crashReports.unsubmittedCheck.shutdownWhileShowing is
 * set to true.
 */
add_task(async function test_shutdown_while_showing() {
  await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  UnsubmittedCrashHandler.uninit();
  let shutdownWhileShowing = UnsubmittedCrashHandler.prefs.getBoolPref(
    "shutdownWhileShowing"
  );
  Assert.ok(
    shutdownWhileShowing,
    "We should have noticed that we uninitted while showing " +
      "the notification."
  );
  UnsubmittedCrashHandler.prefs.clearUserPref("shutdownWhileShowing");
  UnsubmittedCrashHandler.init();

  gNotificationBox.removeNotification(notification, true);
  await clearPendingCrashReports();
});

/**
 * Tests that if UnsubmittedCrashHandler is uninit after
 * the notification has been closed, that
 * browser.crashReports.unsubmittedCheck.shutdownWhileShowing is
 * not set in prefs.
 */
add_task(async function test_shutdown_while_not_showing() {
  let reportIDs = await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  // Dismiss the notification by clicking on the "X" button.
  notification.closeButton.click();
  // We'll not wait for the notification to finish its transition -
  // we'll just remove it right away.
  gNotificationBox.removeNotification(notification, true);

  await waitForIgnoredReports(reportIDs);

  UnsubmittedCrashHandler.uninit();
  Assert.throws(
    () => {
      UnsubmittedCrashHandler.prefs.getBoolPref("shutdownWhileShowing");
    },
    /NS_ERROR_UNEXPECTED/,
    "We should have noticed that the notification had closed before uninitting."
  );
  UnsubmittedCrashHandler.init();

  await clearPendingCrashReports();
});

/**
 * Tests that if
 * browser.crashReports.unsubmittedCheck.shutdownWhileShowing is
 * set and the lastShownDate is today, then we don't decrement
 * browser.crashReports.unsubmittedCheck.chancesUntilSuppress.
 */
add_task(async function test_dont_decrement_chances_on_same_day() {
  let initChances = UnsubmittedCrashHandler.prefs.getIntPref(
    "chancesUntilSuppress"
  );
  Assert.greater(initChances, 1, "We should start with at least 1 chance.");

  await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  UnsubmittedCrashHandler.uninit();

  gNotificationBox.removeNotification(notification, true);

  let shutdownWhileShowing = UnsubmittedCrashHandler.prefs.getBoolPref(
    "shutdownWhileShowing"
  );
  Assert.ok(
    shutdownWhileShowing,
    "We should have noticed that we uninitted while showing " +
      "the notification."
  );

  let today = UnsubmittedCrashHandler.dateString(new Date());
  let lastShownDate =
    UnsubmittedCrashHandler.prefs.getCharPref("lastShownDate");
  Assert.equal(today, lastShownDate, "Last shown date should be today.");

  UnsubmittedCrashHandler.init();

  notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should still be a notification");

  let chances = UnsubmittedCrashHandler.prefs.getIntPref(
    "chancesUntilSuppress"
  );

  Assert.equal(initChances, chances, "We should not have decremented chances.");

  gNotificationBox.removeNotification(notification, true);
  await clearPendingCrashReports();
});

/**
 * Tests that if
 * browser.crashReports.unsubmittedCheck.shutdownWhileShowing is
 * set and the lastShownDate is before today, then we decrement
 * browser.crashReports.unsubmittedCheck.chancesUntilSuppress.
 */
add_task(async function test_decrement_chances_on_other_day() {
  let initChances = UnsubmittedCrashHandler.prefs.getIntPref(
    "chancesUntilSuppress"
  );
  Assert.greater(initChances, 1, "We should start with at least 1 chance.");

  await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should be a notification");

  UnsubmittedCrashHandler.uninit();

  gNotificationBox.removeNotification(notification, true);

  let shutdownWhileShowing = UnsubmittedCrashHandler.prefs.getBoolPref(
    "shutdownWhileShowing"
  );
  Assert.ok(
    shutdownWhileShowing,
    "We should have noticed that we uninitted while showing " +
      "the notification."
  );

  // Now pretend that the notification was shown yesterday.
  let yesterday = UnsubmittedCrashHandler.dateString(
    new Date(Date.now() - DAY)
  );
  UnsubmittedCrashHandler.prefs.setCharPref("lastShownDate", yesterday);

  UnsubmittedCrashHandler.init();

  notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.ok(notification, "There should still be a notification");

  let chances = UnsubmittedCrashHandler.prefs.getIntPref(
    "chancesUntilSuppress"
  );

  Assert.equal(
    initChances - 1,
    chances,
    "We should have decremented our chances."
  );
  UnsubmittedCrashHandler.prefs.clearUserPref("chancesUntilSuppress");

  gNotificationBox.removeNotification(notification, true);
  await clearPendingCrashReports();
});

/**
 * Tests that if we've shutdown too many times showing the
 * notification, and we've run out of chances, then
 * browser.crashReports.unsubmittedCheck.suppressUntilDate is
 * set for some days into the future.
 */
add_task(async function test_can_suppress_after_chances() {
  // Pretend that a notification was shown yesterday.
  let yesterday = UnsubmittedCrashHandler.dateString(
    new Date(Date.now() - DAY)
  );
  UnsubmittedCrashHandler.prefs.setCharPref("lastShownDate", yesterday);
  UnsubmittedCrashHandler.prefs.setBoolPref("shutdownWhileShowing", true);
  UnsubmittedCrashHandler.prefs.setIntPref("chancesUntilSuppress", 0);

  await createPendingCrashReports(1);
  let notification =
    await UnsubmittedCrashHandler.checkForUnsubmittedCrashReports();
  Assert.equal(
    notification,
    null,
    "There should be no notification if we've run out of chances"
  );

  // We should have set suppressUntilDate into the future
  let suppressUntilDate =
    UnsubmittedCrashHandler.prefs.getCharPref("suppressUntilDate");

  let today = UnsubmittedCrashHandler.dateString(new Date());
  // Note: this is comparing strings, and Assert.greater requires inputs to
  // be numbers or dates. bug 1973910 covers fixing this.
  // eslint-disable-next-line mozilla/no-comparison-or-assignment-inside-ok
  Assert.ok(
    suppressUntilDate > today,
    "We should be suppressing until some days into the future."
  );

  UnsubmittedCrashHandler.prefs.clearUserPref("chancesUntilSuppress");
  UnsubmittedCrashHandler.prefs.clearUserPref("suppressUntilDate");
  UnsubmittedCrashHandler.prefs.clearUserPref("lastShownDate");
  await clearPendingCrashReports();
});

/**
 * Tests that if there's a suppression date set, then no notification
 * will be shown even if there are pending crash reports.
 */
add_task(async function test_suppression() {
  let future = UnsubmittedCrashHandler.dateString(
    new Date(Date.now() + DAY * 5)
  );
  UnsubmittedCrashHandler.prefs.setCharPref("suppressUntilDate", future);
  UnsubmittedCrashHandler.uninit();
  UnsubmittedCrashHandler.init();

  Assert.ok(
    UnsubmittedCrashHandler.suppressed,
    "The UnsubmittedCrashHandler should be suppressed."
  );
  UnsubmittedCrashHandler.prefs.clearUserPref("suppressUntilDate");

  UnsubmittedCrashHandler.uninit();
  UnsubmittedCrashHandler.init();
});

/**
 * Tests that if there's a suppression date set, but we've exceeded
 * it, then we can show the notification again.
 */
add_task(async function test_end_suppression() {
  let yesterday = UnsubmittedCrashHandler.dateString(
    new Date(Date.now() - DAY)
  );
  UnsubmittedCrashHandler.prefs.setCharPref("suppressUntilDate", yesterday);
  UnsubmittedCrashHandler.uninit();
  UnsubmittedCrashHandler.init();

  Assert.ok(
    !UnsubmittedCrashHandler.suppressed,
    "The UnsubmittedCrashHandler should not be suppressed."
  );
  Assert.ok(
    !UnsubmittedCrashHandler.prefs.prefHasUserValue("suppressUntilDate"),
    "The suppression date should been cleared from preferences."
  );

  UnsubmittedCrashHandler.uninit();
  UnsubmittedCrashHandler.init();
});
