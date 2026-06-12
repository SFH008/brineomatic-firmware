(function (global) { //private scope

  // work in the global YB namespace.
  var YB = global.YB || {};

  var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  function formatDate(unixSeconds) {
    var d = new Date(unixSeconds * 1000);
    var mm = String(d.getMonth() + 1).padStart(2, '0');
    var dd = String(d.getDate()).padStart(2, '0');
    return d.getFullYear() + '-' + mm + '-' + dd;
  }

  function MaintenanceItem() {
    YB.BaseChannel.call(this, "maintenance", "Maintenance Item");
  }

  MaintenanceItem.prototype = Object.create(YB.BaseChannel.prototype);
  MaintenanceItem.prototype.constructor = MaintenanceItem;

  MaintenanceItem.prototype.generateControlContainer = function () {
    return `
      <div id="${this.channelType}ControlDiv" style="display:none" class="gy-3 mb-3 col-md-12">
          <div id="${this.channelType}Cards" class="row g-3 mb-3"></div>
          <div id="maintenanceLog" class="row mb-3">
            <h3>Maintenance Log</h3>
            <div id="maintenanceLogContent">
            Loading...
            </div>
          </div>
      </div>
    `;
  };

  MaintenanceItem.prototype.generateControlUI = function () {
    return `
      <div id="maintenanceControlCard${this.id}" class="col-12">
        <div class="p-3 border rounded">
          <h4 id="maintenanceName${this.id}" class="mb-3 maintenanceName">${this.name}</h4>
          <div class="row">
            <div id="maintenance-runtimeCol${this.id}" class="col-6 col-md-4">
              <table class="table table-sm mb-0">
                <thead>
                  <tr>
                    <th>Runtime</th>
                    <th>Hours</th>
                  </tr>
                </thead>
                <tr>
                  <td>Last</td>
                  <td><span id="maintenance-lastRuntime${this.id}">—</span></td>
                </tr>
                <tr>
                  <td>Current</td>
                  <td><span id="maintenance-currentRuntime${this.id}">—</span></td>
                </tr>
                <tr id="maintenance-elapsedRuntimeRow${this.id}">
                  <td>Elapsed</td>
                  <td><span id="maintenance-elapsedRuntime${this.id}">—</span></td>
                </tr>
                <tr id="maintenance-nextRuntimeRow${this.id}">
                  <td>Next</td>
                  <td><span id="maintenance-nextRuntime${this.id}">—</span></td>
                </tr>
                <tr id="maintenance-remainingRuntimeRow${this.id}">
                  <td>Remaining</td>
                  <td><span id="maintenance-remainingRuntime${this.id}">—</span></td>
                </tr>
              </table>
            </div>
            <div id="maintenance-timestampCol${this.id}" class="col-6 col-md-4">
              <table class="table table-sm mb-0">
                <thead>
                  <tr>
                    <th>Calendar</th>
                    <th>Date</th>
                  </tr>
                </thead>
                <tr>
                  <td>Last</td>
                  <td><span id="maintenance-lastTimestamp${this.id}">—</span></td>
                </tr>
                <tr>
                  <td>Today</td>
                  <td><span id="maintenance-currentTimestamp${this.id}">—</span></td>
                </tr>
                <tr id="maintenance-elapsedTimestampRow${this.id}">
                  <td>Elapsed</td>
                  <td><span id="maintenance-elapsedTimestamp${this.id}">—</span></td>
                </tr>
                <tr id="maintenance-nextTimestampRow${this.id}">
                  <td>Next</td>
                  <td><span id="maintenance-nextTimestamp${this.id}">—</span></td>
                </tr>
                <tr id="maintenance-remainingTimestampRow${this.id}">
                  <td>Remaining</td>
                  <td><span id="maintenance-remainingTimestamp${this.id}">—</span></td>
                </tr>
              </table>
            </div>
            <div id="maintenance-btnCol${this.id}" class="col-12 col-md-4 text-center align-self-center mt-3 mt-md-0" style="display:none;">
              <button class="btn btn-primary" type="button" id="maintenance-markComplete${this.id}">Log Maintenance Complete</button>
            </div>
            <div id="maintenance-noInterval${this.id}" class="col-12 text-danger text-center" style="display:none;">Error: no maintenance interval set.</div>
          </div>
        </div>
      </div>

      <div class="modal fade" id="maintenanceConfirmModal${this.id}" tabindex="-1" aria-hidden="true">
        <div class="modal-dialog">
          <div class="modal-content">
            <div class="modal-header">
              <h5 class="modal-title">Log Maintenance As Complete</h5>
              <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
            </div>
            <div class="modal-body">
              <p>Mark <strong>${this.name}</strong> as maintenance complete?</p>
              <div class="mb-3">
                <label for="maintenanceNotes${this.id}" class="form-label">Notes</label>
                <textarea class="form-control" id="maintenanceNotes${this.id}" rows="3" placeholder="Optional notes..."></textarea>
              </div>
            </div>
            <div class="modal-footer">
              <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
              <button type="button" class="btn btn-primary" id="maintenanceConfirmOk${this.id}">OK</button>
            </div>
          </div>
        </div>
      </div>
    `;
  };

  MaintenanceItem.prototype.setupControlUI = function () {
    YB.BaseChannel.prototype.setupControlUI.call(this);

    $(`#maintenance-markComplete${this.id}`).click(this.onMarkComplete.bind(this));
    $(`#maintenanceConfirmOk${this.id}`).click(this.recordMaintenance.bind(this));
  };

  MaintenanceItem.prototype.onMarkComplete = function (e) {
    $(e.currentTarget).blur();
    $(`#maintenanceNotes${this.id}`).val('');
    new bootstrap.Modal($(`#maintenanceConfirmModal${this.id}`)[0]).show();
  };

  MaintenanceItem.prototype.recordMaintenance = function () {
    var notes = $(`#maintenanceNotes${this.id}`).val().trim();
    bootstrap.Modal.getInstance($(`#maintenanceConfirmModal${this.id}`)[0]).hide();

    let maintenancePage = YB.App.getPage("maintenance");
    if (maintenancePage)
      maintenancePage.clearBadge();

    YB.client.send({
      "cmd": "record_maintenance",
      "id": this.id,
      "notes": notes,
      "timestamp": Math.floor(Date.now() / 1000),
    }, true);

    setTimeout(MaintenanceItem.loadMaintenanceLog, 1000);
  };

  MaintenanceItem.prototype.updateControlUI = function () {
    $(`#maintenanceControlCard${this.id}`).toggle(this.enabled);

    if (!this.enabled)
      return;

    var showRuntime = this.cfg.runtimeInterval > 0;
    var showTimestamp = this.cfg.timestampInterval > 0;
    var hasInterval = showRuntime || showTimestamp;

    var $runtimeCol = $(`#maintenance-runtimeCol${this.id}`);
    var $timestampCol = $(`#maintenance-timestampCol${this.id}`);
    var $btnCol = $(`#maintenance-btnCol${this.id}`);

    // $runtimeCol.toggle(showRuntime);
    // $timestampCol.toggle(showTimestamp);
    $btnCol.toggle(YB.App.role == 'admin');

    $runtimeCol.removeClass('col-6 col col-md-4 col-md-8');
    $timestampCol.removeClass('col-6 col col-md-4 col-md-8');
    $btnCol.removeClass('col-12 col col-md-4');

    if (YB.App.role == 'admin') {
      // if (showRuntime && showTimestamp) {
      $runtimeCol.addClass('col-6 col-md-4');
      $timestampCol.addClass('col-6 col-md-4');
      $btnCol.addClass('col-12 col-md-4');
      // } else {
      //   if (showRuntime) $runtimeCol.addClass('col col-md-8');
      //   if (showTimestamp) $timestampCol.addClass('col col-md-8');
      //   $btnCol.addClass('col col-md-4');
      // }
    } else {
      $runtimeCol.addClass('col-6');
      $timestampCol.addClass('col-6');
    }

    //clear our badge
    let maintenancePage = YB.App.getPage("maintenance");

    $(`#maintenanceControlCard${this.id} div`).removeClass("border-danger").addClass("border-secondary");

    if (YB.Brineomatic.totalRuntime) {
      let totalRuntime = parseFloat(YB.Brineomatic.totalRuntime);
      let lastRuntime = this.data.lastRuntime;
      let elapsedRuntime = Math.max(0, totalRuntime - lastRuntime);
      $(`#maintenance-lastRuntime${this.id}`).text(lastRuntime.toFixed(1));
      $(`#maintenance-currentRuntime${this.id}`).text(totalRuntime.toFixed(1));
      $(`#maintenance-elapsedRuntime${this.id}`).text(elapsedRuntime.toFixed(1));

      $(`#maintenance-nextRuntimeRow${this.id}`).toggle(showRuntime);
      $(`#maintenance-remainingRuntimeRow${this.id}`).toggle(showRuntime);

      if (showRuntime) {
        let nextRuntime = lastRuntime + this.cfg.runtimeInterval;
        let remainingRuntime = nextRuntime - totalRuntime;
        let runtimeOverdue = totalRuntime >= nextRuntime;

        $(`#maintenance-nextRuntime${this.id}`).text(nextRuntime.toFixed(1));
        $(`#maintenance-remainingRuntime${this.id}`).text(Math.abs(remainingRuntime).toFixed(1));
        $(`#maintenance-remainingRuntimeRow${this.id} td:first`).text(runtimeOverdue ? 'Overdue' : 'Remaining');
        $(`#maintenance-nextRuntimeRow${this.id} td`).removeClass('text-success text-danger').addClass(runtimeOverdue ? 'text-danger' : 'text-success');
        $(`#maintenance-remainingRuntimeRow${this.id} td`).removeClass('text-success text-danger').addClass(runtimeOverdue ? 'text-danger' : 'text-success');

        if (runtimeOverdue && maintenancePage) {
          $(`#maintenanceControlCard${this.id} div`).removeClass("border-secondary").addClass("border-danger");
          maintenancePage.setBadge("danger");
        }
      }

      let lastTimestamp = this.data.lastTimestamp;
      let nowSeconds = Math.round(Date.now() / 1000);
      let elapsedDays = Math.round((nowSeconds - lastTimestamp) / (60 * 60 * 24));
      $(`#maintenance-lastTimestamp${this.id}`).text(formatDate(lastTimestamp));
      $(`#maintenance-currentTimestamp${this.id}`).text(formatDate(nowSeconds));
      $(`#maintenance-elapsedTimestamp${this.id}`).text(`${elapsedDays} days`);

      $(`#maintenance-nextTimestampRow${this.id}`).toggle(showTimestamp);
      $(`#maintenance-remainingTimestampRow${this.id}`).toggle(showTimestamp);

      if (showTimestamp) {
        let nextTimestamp = lastTimestamp + this.cfg.timestampInterval;
        let remainingDays = Math.round((nextTimestamp - nowSeconds) / (60 * 60 * 24));
        let timestampOverdue = nowSeconds >= nextTimestamp;

        $(`#maintenance-nextTimestamp${this.id}`).text(formatDate(nextTimestamp));
        $(`#maintenance-remainingTimestamp${this.id}`).text(`${Math.abs(remainingDays)} days`);
        $(`#maintenance-remainingTimestampRow${this.id} td:first`).text(timestampOverdue ? 'Overdue' : 'Remaining');
        $(`#maintenance-nextTimestampRow${this.id} td`).removeClass('text-success text-danger').addClass(timestampOverdue ? 'text-danger' : 'text-success');
        $(`#maintenance-remainingTimestampRow${this.id} td`).removeClass('text-success text-danger').addClass(timestampOverdue ? 'text-danger' : 'text-success');

        if (timestampOverdue && maintenancePage) {
          $(`#maintenanceControlCard${this.id} div`).removeClass("border-secondary").addClass("border-danger");
          maintenancePage.setBadge("danger");
        }
      }
    }
  };

  MaintenanceItem.prototype.getConfigSchema = function () {
    // copy base schema to avoid mutating the base literal
    var base = YB.BaseChannel.prototype.getConfigSchema.call(this);

    var schema = Object.assign({}, base);

    schema.runtimeInterval = {
      presence: true,
      numericality: {
        greaterThanOrEqualTo: 0.0,
        lessThanOrEqualTo: 5000.0
      }
    };

    schema.lastRuntime = {
      presence: true,
      numericality: {
        greaterThanOrEqualTo: 0.0
      }
    };

    schema.timestampInterval = {
      presence: true,
      numericality: {
        greaterThanOrEqualTo: 0,
        lessThanOrEqualTo: (60 * 60 * 24 * 1000)
      }
    };

    schema.lastTimestamp = {
      presence: true,
      numericality: {
        greaterThanOrEqualTo: 0,
        lessThanOrEqualTo: 4294967295
      }
    };

    return schema;
  };

  MaintenanceItem.prototype.getConfigFormData = function () {
    let newcfg = YB.BaseChannel.prototype.getConfigFormData.call(this);

    newcfg.runtimeInterval = parseFloat($(`#f-maintenance-runtimeInterval-${this.id}`).val());
    newcfg.lastRuntime = parseFloat($(`#f-maintenance-lastRuntime-${this.id}`).val());
    newcfg.timestampInterval = Math.round(parseFloat($(`#f-maintenance-timestampInterval-${this.id}`).val()) * (24 * 60 * 60));
    newcfg.lastTimestamp = parseInt($(`#f-maintenance-lastTimestamp-${this.id}`).val());

    return newcfg;
  }

  MaintenanceItem.prototype.onEditForm = function (e) {
    YB.BaseChannel.prototype.onEditForm.call(this, e);

    //ui updates
    $(`#f-maintenance-runtimeInterval-${this.id}`).prop('disabled', !this.enabled);
    $(`#f-maintenance-lastRuntime-${this.id}`).prop('disabled', !this.enabled);
    $(`#f-maintenance-timestampInterval-${this.id}`).prop('disabled', !this.enabled);
    $(`#f-maintenance-lastTimestamp-${this.id}`).prop('disabled', !this.enabled);
  };

  MaintenanceItem.prototype.generateEditUI = function () {

    let standardFields = this.generateStandardEditFields();

    return `
      <div id="maintenanceEditCard${this.id}" class="col-12 mb-3">
        <div class="p-3 border border-secondary rounded">
          <h5>Maintenance Item #${this.id}</h5>
          ${standardFields}

          <div class="form-floating mb-3">
            <input type="text" class="form-control" id="f-maintenance-runtimeInterval-${this.id}" value="${this.cfg.runtimeInterval}">
            <label for="f-maintenance-runtimeInterval-${this.id}">Runtime Interval (hours)</label>
            <div class="invalid-feedback"></div>
          </div>

          <div class="form-floating mb-3">
            <input type="text" class="form-control" id="f-maintenance-timestampInterval-${this.id}" value="${this.cfg.timestampInterval}">
            <label for="f-maintenance-timestampInterval-${this.id}">Time Interval (days)</label>
            <div class="invalid-feedback"></div>
          </div>

          <div class="form-floating mb-3">
            <input type="text" class="form-control" id="f-maintenance-lastRuntime-${this.id}" value="${this.cfg.lastRuntime}">
            <label for="f-maintenance-lastRuntime-${this.id}">Last Runtime (hours)</label>
            <div class="invalid-feedback"></div>
          </div> 

          <div class="form-floating mb-3">
            <input type="text" class="form-control" id="f-maintenance-lastTimestamp-${this.id}" value="${this.cfg.lastTimestamp}">
            <label for="f-maintenance-lastTimestamp-${this.id}">Last Maintenance (timestamp)</label>
            <div class="invalid-feedback"></div>
          </div>
        </div>
      </div>
    `;
  };

  MaintenanceItem.prototype.generateEditContainer = function () {
    let panel = YB.BaseChannel.prototype.generateEditContainer.call(this);
    panel.displayName = "Maintenance";
    return panel;
  };

  MaintenanceItem.prototype.setupEditUI = function () {
    YB.BaseChannel.prototype.setupEditUI.call(this);

    $(`#f-maintenance-runtimeInterval-${this.id}`).val(this.cfg.runtimeInterval);
    $(`#f-maintenance-timestampInterval-${this.id}`).val(Math.round(this.cfg.timestampInterval / (24 * 60 * 60)));
    $(`#f-maintenance-lastRuntime-${this.id}`).val(this.cfg.lastRuntime);
    $(`#f-maintenance-lastTimestamp-${this.id}`).val(this.cfg.lastTimestamp);

    $(`#f-maintenance-runtimeInterval-${this.id}`).prop('disabled', !this.enabled);
    $(`#f-maintenance-timestampInterval-${this.id}`).prop('disabled', !this.enabled);
    $(`#f-maintenance-lastRuntime-${this.id}`).prop('disabled', !this.enabled);
    $(`#f-maintenance-lastTimestamp-${this.id}`).prop('disabled', !this.enabled);

    $(`#f-maintenance-runtimeInterval-${this.id}`).change(this.onEditForm);
    $(`#f-maintenance-timestampInterval-${this.id}`).change(this.onEditForm);
    $(`#f-maintenance-lastRuntime-${this.id}`).change(this.onEditForm);
    $(`#f-maintenance-lastTimestamp-${this.id}`).change(this.onEditForm);
  };

  MaintenanceItem.loadMaintenanceLog = function () {

    if (YB.Brineomatic.totalRuntime === undefined) {
      setTimeout(MaintenanceItem.loadMaintenanceLog, 100);
      return;
    }

    $.ajax({
      url: '/maintenance.json',
      dataType: 'text',
      success: function (text) {
        var lines = text.trim().split('\n').filter(function (l) { return l.trim(); }).reverse();

        $('#maintenanceLogContent').html(`
          <div class="yarrboardLog" id="maintenanceLogGrid"></div>
          <div class="d-flex justify-content-center mt-3">
            <a href="/maintenance.json" class="btn btn-primary d-inline-flex align-items-center gap-2">
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-download" viewBox="0 0 16 16">
                <path d="M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5"></path>
                <path d="M7.646 11.854a.5.5 0 0 0 .708 0l3-3a.5.5 0 0 0-.708-.708L8.5 10.293V1.5a.5.5 0 0 0-1 0v8.793L5.354 8.146a.5.5 0 1 0-.708.708z"></path>
              </svg>
              Download Maintenance Log as JSON
            </a>
          </div>
        `);

        if (!lines.length) {
          $('#maintenanceLogGrid').html('<p class="text-muted">No maintenance events recorded.</p>');
          let maintenance = YB.App.getPage("maintenance");
          maintenance.ready = true;
          return;
        }

        var data = lines.map(function (line) {
          var entry = JSON.parse(line);
          return [
            entry.name || '',
            entry.runtime.toFixed(1) + ' hrs',
            formatDate(entry.timestamp),
            entry.notes || ''
          ];
        });

        new gridjs.Grid({
          columns: ['Name', 'Runtime', 'Date', 'Notes'],
          data: data,
          search: {
            selector: (cell, rowIndex, cellIndex) => {
              // Only search the first 3 columns
              if (cellIndex === 0 || cellIndex === 2 || cellIndex === 3) return cell;
              // Return nothing for other columns
              return null;
            }
          },
          pagination: { limit: 25 },
          sort: true,
        }).render(document.getElementById('maintenanceLogGrid'));

        let maintenance = YB.App.getPage("maintenance");
        maintenance.ready = true;
      },
      error: function () {
        $('#maintenanceLogContent').html('<p class="text-danger">No maintenance log found.</p>');
        let maintenance = YB.App.getPage("maintenance");
        maintenance.ready = true;
      }
    });
  };

  MaintenanceItem.deleteLogs = function () {
    if (confirm("Are you sure you want to delete your maintenance logs?  This cannot be reversed.")) {
      YB.App.showAlert("Maintenance logs have been deleted.", "primary");
      YB.client.send({
        "cmd": "maintenance_delete_logs"
      }, true);
    }
  };

  YB.MaintenanceItem = MaintenanceItem;
  YB.ChannelRegistry.registerChannelType("maintenance", YB.MaintenanceItem)

  // Create a custom page
  let maintenancePage = new YB.Page({
    name: 'maintenance',
    displayName: 'Maintenance',
    permissionLevel: 'guest',
    showInNavbar: true,
    position: "logs",
    ready: false,
    content: ""
  });

  // Add our open / close handlers and the page itself
  maintenancePage.onOpen(function () {
    YB.App.getStatsData();
    YB.App.startUpdatePoller();
    MaintenanceItem.loadMaintenanceLog();
  });
  maintenancePage.onClose(YB.App.stopUpdatePoller);

  YB.App.addPage(maintenancePage);

  //get totalRuntime
  YB.App.onStart(function () {
    setTimeout(YB.App.getStatsData, 1000);

    let deleteButton = `
      <button id="deleteMaintenanceLogsButton" class="btn btn-warning" type="button">
        Delete Maintenance Logs
      </button>
    `;
    $("#dangerZone").prepend(deleteButton);
    $("#deleteMaintenanceLogsButton").on("click", YB.MaintenanceItem.deleteLogs);
  });

  global.YB = YB; // <-- this line makes it global

})(this); //private scope