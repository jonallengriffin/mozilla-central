/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

const kMARIONETTE_CONTRACTID = "@mozilla.org/marionette;1";
const kMARIONETTE_CID = Components.ID("{786a1369-dca5-4adc-8486-33d23c88010a}");

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import('resource:///modules/marionette-logger.jsm');
MarionetteLogger.write('MarionetteComponent loaded');

function MarionetteComponent() {
  this._loaded = false;
}

MarionetteComponent.prototype = {
  classDescription: "Marionette component",
  classID: kMARIONETTE_CID,
  contractID: kMARIONETTE_CONTRACTID,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver, Ci.nsIMarionette]),
  _xpcom_categories: [{category: "profile-after-change", service: true}],

  observe: function mm_observe(aSubject, aTopic, aData) {
    let observerService = Services.obs;
    switch (aTopic) {
      case "profile-after-change":
        Services.prefs.addObserver('marionette.defaultPrefs.enabled', this, false);
        if (Services.prefs.getBoolPref('marionette.defaultPrefs.enabled')) {
          observerService.addObserver(this, "final-ui-startup", false);
          observerService.addObserver(this, "xpcom-shutdown", false);
        }
        else {
          MarionetteLogger.write("marionette not enabled");
        }
        break;
      case "nsPref:changed":
        Services.prefs.setBoolPref("marionette.defaultPrefs.enabled", false);
        MarionetteLogger.write("Something tried to change marionette.defaultPrefs.enabled; defaulting to false");
        break;
      case "final-ui-startup":
        observerService.removeObserver(this, "final-ui-startup");
        this.init();
        break;
      case "xpcom-shutdown":
        observerService.removeObserver(this, "xpcom-shutdown");
        this.uninit();
        break;
    }
  },

  init: function mm_init() {
    if (!this._loaded) {
      this._loaded = true;
      let port;
      try {
        port = Services.prefs.getIntPref('marionette.defaultPrefs.port');
      }
      catch(e) {
        port = 2828;
      }
      try {
        Cu.import('resource:///modules/devtools/dbg-server.jsm');
        DebuggerServer.addActors('chrome://marionette/content/marionette-actors.js');
        DebuggerServer.initTransport();
        DebuggerServer.openListener(port, true);
      }
      catch(e) {
        MarionetteLogger.write('exception: ' + e.name + ', ' + e.message);
      }
    }
  },

  uninit: function mm_uninit() {
    DebuggerServer.closeListener();
    this._loaded = false;
  },

};

const NSGetFactory = XPCOMUtils.generateNSGetFactory([MarionetteComponent]);
