(function (global) { // private scope
  // work in the global YB namespace.
  var YB = global.YB || {};

  //
  // Home page: the live sensor gauges.
  //
  // Owns the c3 gauge instances on the home page plus the drag-to-reorder /
  // show-hide edit UI that lets the user arrange which gauges are shown.  A
  // single instance lives on the Brineomatic object (YB.bom.gauges), created
  // lazily once the first config arrives (SensorGauges.js may load after
  // Brineomatic.js, so we can't build it at parse time).
  //
  // Reaches back through this.bom for unit conversions and short-unit labels,
  // and reads the shared sensor configuration table (this.bom.sensorConfig)
  // for each gauge's min/max/thresholds/colors.  That same table is consumed
  // by SensorGraphs (y-axis ranges) and by Brineomatic.setDataColor (text-tile
  // colouring), so it lives on Brineomatic rather than here.
  //
  function SensorGauges(bom) {
    this.bom = bom;

    // every gauge tile's data-gauge key, in their default order; drives the
    // add/hide UI and the saved-order restore.  productVolume/flushVolume are
    // text tiles (not c3 gauges) but still participate in the reorder UI.
    this.gaugeAllKeys = [
      'filterPressure', 'membranePressure', 'productSalinity', 'productFlowrate',
      'brineSalinity', 'brineFlowrate', 'totalFlowrate', 'motorTemperature',
      'waterTemperature', 'tankLevel', 'batteryLevel', 'productVolume', 'flushVolume'
    ];
    this.gaugeEditMode = false;
    this.sortableGauges = null;
    this.sortableGaugesMFD = null;
  }

  // Build the 11 c3 gauge instances from the shared sensor config.  Idempotent:
  // handleConfigMessage can fire more than once (reconnect), so bail if the
  // gauges already exist rather than orphaning the old c3 charts.
  SensorGauges.prototype.create = function () {
    if (this.motorTemperatureGauge)
      return;

    const cfg = this.bom.sensorConfig;

    this.motorTemperatureGauge = c3.generate({
      bindto: '#motorTemperatureGauge',
      data: {
        columns: [
          ['Motor Temperature', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            let short = YB.bom.getShortTemperatureUnits(YB.config.brineomatic.temperature_units);
            return `${value}°${short}`;
          },
          show: true
        },
        min: cfg.motor_temperature.min,
        max: cfg.motor_temperature.max
      },
      color: {
        pattern: cfg.motor_temperature.colors,
        threshold: {
          unit: 'value',
          values: cfg.motor_temperature.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.waterTemperatureGauge = c3.generate({
      bindto: '#waterTemperatureGauge',
      data: {
        columns: [
          ['Water Temperature', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            let short = YB.bom.getShortTemperatureUnits(YB.config.brineomatic.temperature_units);
            return `${value}°${short}`;
          },
          show: true
        },
        min: cfg.water_temperature.min,
        max: cfg.water_temperature.max
      },
      color: {
        pattern: cfg.water_temperature.colors,
        threshold: {
          unit: 'value',
          values: cfg.water_temperature.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.filterPressureGauge = c3.generate({
      bindto: '#filterPressureGauge',
      data: {
        columns: [
          ['Filter Pressure', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            let short = YB.bom.getShortPressureUnits(YB.config.brineomatic.pressure_units);
            return `${value} ${short}`;
          },
          show: true
        },
        min: cfg.filter_pressure.min,
        max: cfg.filter_pressure.max,
      },
      color: {
        pattern: cfg.filter_pressure.colors,
        threshold: {
          unit: 'value',
          values: cfg.filter_pressure.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.membranePressureGauge = c3.generate({
      bindto: '#membranePressureGauge',
      data: {
        columns: [
          ['Membrane Pressure', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            let short = YB.bom.getShortPressureUnits(YB.config.brineomatic.pressure_units);
            return `${value} ${short}`;
          },
          show: true
        },
        min: cfg.membrane_pressure.min,
        max: cfg.membrane_pressure.max,
      },
      color: {
        pattern: cfg.membrane_pressure.colors,
        threshold: {
          unit: 'value',
          values: cfg.membrane_pressure.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.productSalinityGauge = c3.generate({
      bindto: '#productSalinityGauge',
      data: {
        columns: [
          ['Product Salinity', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            return `${value} PPM`;
          },
          show: true
        },
        min: cfg.product_salinity.min,
        max: cfg.product_salinity.max,
      },
      color: {
        pattern: cfg.product_salinity.colors,
        threshold: {
          unit: 'value',
          values: cfg.product_salinity.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.brineSalinityGauge = c3.generate({
      bindto: '#brineSalinityGauge',
      data: {
        columns: [
          ['Brine Salinity', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            return `${value} PPM`;
          },
          show: true
        },
        min: cfg.brine_salinity.min,
        max: cfg.brine_salinity.max,
      },
      color: {
        pattern: cfg.brine_salinity.colors,
        threshold: {
          unit: 'value',
          values: cfg.brine_salinity.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.productFlowrateGauge = c3.generate({
      bindto: '#productFlowrateGauge',
      data: {
        columns: [
          ['Product Flowrate', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            let short = YB.bom.getShortFlowrateUnits(YB.config.brineomatic.flowrate_units);
            return `${value} ${short}`;
          },
          show: true
        },
        min: cfg.product_flowrate.min,
        max: cfg.product_flowrate.max,
      },
      color: {
        pattern: cfg.product_flowrate.colors,
        threshold: {
          unit: 'value',
          values: cfg.product_flowrate.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.brineFlowrateGauge = c3.generate({
      bindto: '#brineFlowrateGauge',
      data: {
        columns: [
          ['Brine Flowrate', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            let short = YB.bom.getShortFlowrateUnits(YB.config.brineomatic.flowrate_units);
            return `${value} ${short}`;
          },
          show: true
        },
        min: cfg.brine_flowrate.min,
        max: cfg.brine_flowrate.max,
      },
      color: {
        pattern: cfg.brine_flowrate.colors,
        threshold: {
          unit: 'value',
          values: cfg.brine_flowrate.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.totalFlowrateGauge = c3.generate({
      bindto: '#totalFlowrateGauge',
      data: {
        columns: [
          ['Total Flowrate', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            let short = YB.bom.getShortFlowrateUnits(YB.config.brineomatic.flowrate_units);
            return `${value} ${short}`;
          },
          show: true
        },
        min: cfg.total_flowrate.min,
        max: cfg.total_flowrate.max,
      },
      color: {
        pattern: cfg.total_flowrate.colors,
        threshold: {
          unit: 'value',
          values: cfg.total_flowrate.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.tankLevelGauge = c3.generate({
      bindto: '#tankLevelGauge',
      data: {
        columns: [
          ['Tank Level', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            return `${value}%`;
          },
          show: true
        },
        min: cfg.tank_level.min,
        max: cfg.tank_level.max,
      },
      color: {
        pattern: cfg.tank_level.colors,
        threshold: {
          unit: 'value',
          values: cfg.tank_level.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });

    this.batteryLevelGauge = c3.generate({
      bindto: '#batteryLevelGauge',
      data: {
        columns: [
          ['Battery Level', 0]
        ],
        type: 'gauge',
      },
      gauge: {
        label: {
          format: function (value, ratio) {
            return `${value}%`;
          },
          show: true
        },
        min: cfg.battery_level.min,
        max: cfg.battery_level.max,
      },
      color: {
        pattern: cfg.battery_level.colors,
        threshold: {
          unit: 'value',
          values: cfg.battery_level.thresholds
        }
      },
      size: { height: 130, width: 200 },
      interaction: { enabled: false },
      transition: { duration: 0 },
      legend: { hide: true }
    });
  };

  // Push fresh readings into the gauges.  The caller (handleUpdateMessage) has
  // already converted, formatted, and clamped these values — gauges show the
  // same finished numbers as the rest of the UI, so we don't reconvert here.
  SensorGauges.prototype.update = function (values) {
    if (!this.motorTemperatureGauge)
      return;

    this.motorTemperatureGauge.load({ columns: [['Motor Temperature', values.motor_temperature]] });
    this.waterTemperatureGauge.load({ columns: [['Water Temperature', values.water_temperature]] });
    this.filterPressureGauge.load({ columns: [['Filter Pressure', values.filter_pressure]] });
    this.membranePressureGauge.load({ columns: [['Membrane Pressure', values.membrane_pressure]] });
    this.productSalinityGauge.load({ columns: [['Product Salinity', values.product_salinity]] });
    this.brineSalinityGauge.load({ columns: [['Brine Salinity', values.brine_salinity]] });
    this.productFlowrateGauge.load({ columns: [['Product Flowrate', values.product_flowrate]] });
    this.brineFlowrateGauge.load({ columns: [['Brine Flowrate', values.brine_flowrate]] });
    this.totalFlowrateGauge.load({ columns: [['Total Flowrate', values.total_flowrate]] });
    this.tankLevelGauge.load({ columns: [['Tank Level', values.tank_level]] });
    this.batteryLevelGauge.load({ columns: [['Battery Level', values.battery_level]] });
  };

  // Apply a saved gauge order: hide any gauge not in the saved list and reorder
  // the tiles in both the regular and MFD containers to match.
  SensorGauges.prototype.restoreOrder = function (savedOrder) {
    // Hide gauges not in saved order
    this.gaugeAllKeys.forEach(key => {
      if (!savedOrder.includes(key))
        $('[data-gauge="' + key + '"]').addClass('d-none');
    });

    // Reorder tiles in both containers to match saved order
    ['#bomGauges', '#bomGaugesMFD'].forEach(containerId => {
      const $container = $(containerId);
      savedOrder.forEach(key => {
        $container.find('[data-gauge="' + key + '"]').appendTo($container);
      });
    });
  };

  SensorGauges.prototype.toggleGaugeEditMode = function () {
    if (!this.gaugeEditMode)
      this.startGaugeEditMode();
    else
      this.endGaugeEditMode();
  }

  SensorGauges.prototype.startGaugeEditMode = function () {

    if (YB.App.isMFD())
      return;

    this.gaugeEditMode = true;
    $('#bomGauges, #bomGaugesMFD').addClass('gauge-editing');

    this.sortableGauges = Sortable.create(document.getElementById('bomGauges'), {
      animation: 150,
      filter: '.gauge-hide-btn',
      preventOnFilter: false,
      onEnd: () => this.syncMFDOrder()
    });
    this.sortableGaugesMFD = Sortable.create(document.getElementById('bomGaugesMFD'), {
      animation: 150,
      filter: '.gauge-hide-btn',
      preventOnFilter: false,
      onEnd: () => this.syncGaugesOrder()
    });

    // Wire X buttons
    $('#bomGauges .gauge-hide-btn').off('click.gaugeEdit').on('click.gaugeEdit', (e) => {
      const key = $(e.target).closest('[data-gauge]').data('gauge');
      this.hideGauge(key);
      this.renderAddGaugeTile();
    });

    const moveOverlay = '<div class="gauge-move-overlay">' +
      '<svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" class="bi bi-arrows-move" viewBox="0 0 16 16">' +
      '<path fill-rule="evenodd" d="M7.646.146a.5.5 0 0 1 .708 0l2 2a.5.5 0 0 1-.708.708L8.5 1.707V5.5a.5.5 0 0 1-1 0V1.707L6.354 2.854a.5.5 0 1 1-.708-.708zM8 10a.5.5 0 0 1 .5.5v3.793l1.146-1.147a.5.5 0 0 1 .708.708l-2 2a.5.5 0 0 1-.708 0l-2-2a.5.5 0 0 1 .708-.708L7.5 14.293V10.5A.5.5 0 0 1 8 10M.146 8.354a.5.5 0 0 1 0-.708l2-2a.5.5 0 1 1 .708.708L1.707 7.5H5.5a.5.5 0 0 1 0 1H1.707l1.147 1.146a.5.5 0 0 1-.708.708zM10 8a.5.5 0 0 1 .5-.5h3.793l-1.147-1.146a.5.5 0 0 1 .708-.708l2 2a.5.5 0 0 1 0 .708l-2 2a.5.5 0 0 1-.708-.708L14.293 8.5H10.5A.5.5 0 0 1 10 8"/>' +
      '</svg></div>';
    $('#bomGauges .bomGaugeItem, #bomGaugesMFD .bomGaugeItem').append(moveOverlay);

    this.renderAddGaugeTile();
  };

  SensorGauges.prototype.endGaugeEditMode = function () {

    if (YB.App.isMFD())
      return;

    this.gaugeEditMode = false;
    $('#bomGauges, #bomGaugesMFD').removeClass('gauge-editing');

    if (this.sortableGauges) { this.sortableGauges.destroy(); this.sortableGauges = null; }
    if (this.sortableGaugesMFD) { this.sortableGaugesMFD.destroy(); this.sortableGaugesMFD = null; }

    $('#bomAddGaugeTile, #bomAddGaugeTileMFD').remove();
    $('.gauge-move-overlay').remove();
  };

  SensorGauges.prototype.hideGauge = function (key) {
    $('[data-gauge="' + key + '"]').addClass('d-none');
    const order = $('#bomGauges [data-gauge]:not(.d-none)').map((_i, el) => el.dataset.gauge).toArray();
    this.onGaugeOrderChanged(order);
  };

  SensorGauges.prototype.showGauge = function (key) {
    const gaugesTile = $('#bomGauges [data-gauge="' + key + '"]');
    const mfdTile = $('#bomGaugesMFD [data-gauge="' + key + '"]');
    gaugesTile.removeClass('d-none').insertBefore('#bomAddGaugeTile');
    mfdTile.removeClass('d-none').insertBefore('#bomAddGaugeTileMFD');
    this.renderAddGaugeTile();
    const order = $('#bomGauges [data-gauge]:not(.d-none)').map((_i, el) => el.dataset.gauge).toArray();
    this.onGaugeOrderChanged(order);
  };

  SensorGauges.prototype.renderAddGaugeTile = function () {
    const gaugeLabels = {
      filterPressure: 'Filter Pressure',
      membranePressure: 'Membrane Pressure',
      productSalinity: 'Product Salinity',
      productFlowrate: 'Product Flowrate',
      brineSalinity: 'Brine Salinity',
      brineFlowrate: 'Brine Flowrate',
      totalFlowrate: 'Total Flowrate',
      motorTemperature: 'Motor Temperature',
      waterTemperature: 'Water Temperature',
      tankLevel: 'Tank Level',
      batteryLevel: 'Battery Level',
      productVolume: 'Product Volume',
      flushVolume: 'Flush Volume'
    };

    const hiddenKeys = this.gaugeAllKeys.filter(key =>
      $('#bomGauges [data-gauge="' + key + '"]').hasClass('d-none')
    );

    // Populate modal body
    let modalBodyHtml = '';
    if (hiddenKeys.length === 0) {
      modalBodyHtml = '<p class="text-body-tertiary text-center">All gauges visible</p>';
    } else {
      modalBodyHtml = '<div class="d-grid gap-2" style="grid-template-columns: 1fr 1fr;">' +
        hiddenKeys.map(key =>
          '<button class="btn btn-outline-primary bomAddGaugeBtn" data-add-gauge="' + key + '">' +
          gaugeLabels[key] + '</button>'
        ).join('') +
        '</div>';
    }
    $('#addGaugeModalBody').html(modalBodyHtml);

    const tileInner = '<div class="bomAddGaugeTile d-flex flex-column align-items-center justify-content-center p-2">' +
      '<div class="bom-add-gauge-plus">+</div>' +
      '<h6 class="my-0">Add Gauge</h6></div>';

    const tileHtml = '<div id="bomAddGaugeTile" class="bomGaugeItem col-md-3 col-sm-4 col-6"' +
      ' data-bs-toggle="modal" data-bs-target="#addGaugeModal">' + tileInner + '</div>';

    const mfdTileHtml = '<div id="bomAddGaugeTileMFD" class="bomGaugeItem col-md-3 col-sm-4 col-6"' +
      ' data-bs-toggle="modal" data-bs-target="#addGaugeModal">' + tileInner + '</div>';

    $('#bomAddGaugeTile').remove();
    $('#bomAddGaugeTileMFD').remove();
    if (hiddenKeys.length > 0) {
      $('#bomGauges').append(tileHtml);
      $('#bomGaugesMFD').append(mfdTileHtml);
    }

    // Wire add buttons in modal
    $('#addGaugeModalBody').on('click', '.bomAddGaugeBtn', (e) => {
      const key = $(e.currentTarget).data('add-gauge');
      bootstrap.Modal.getInstance(document.getElementById('addGaugeModal')).hide();
      this.showGauge(key);
    });
  };

  SensorGauges.prototype.syncMFDOrder = function () {
    const order = $('#bomGauges [data-gauge]').map((_i, el) => el.dataset.gauge).toArray();
    const mfd = $('#bomGaugesMFD');
    order.forEach(key => mfd.append(mfd.find('[data-gauge="' + key + '"]')));
    const visibleOrder = $('#bomGauges [data-gauge]:not(.d-none)').map((_i, el) => el.dataset.gauge).toArray();
    this.onGaugeOrderChanged(visibleOrder);
  };

  SensorGauges.prototype.syncGaugesOrder = function () {
    const order = $('#bomGaugesMFD [data-gauge]').map((_i, el) => el.dataset.gauge).toArray();
    const gauges = $('#bomGauges');
    order.forEach(key => gauges.append(gauges.find('[data-gauge="' + key + '"]')));
    const visibleOrder = $('#bomGauges [data-gauge]:not(.d-none)').map((_i, el) => el.dataset.gauge).toArray();
    this.onGaugeOrderChanged(visibleOrder);
  };

  SensorGauges.prototype.applyGaugeOrder = function (order) {
    for (const containerId of ['#bomGauges', '#bomGaugesMFD']) {
      const container = $(containerId);
      order.forEach(key => container.append(container.find('[data-gauge="' + key + '"]')));
      container.find('[data-gauge]').each((_i, item) => {
        const key = item.dataset.gauge;
        $(item).toggleClass('d-none', !order.includes(key));
      });
    }
  };

  SensorGauges.prototype.onGaugeOrderChanged = function (order) {
    let data = {};
    data["cmd"] = "brineomatic_save_ui_config";
    data["gauge_order"] = JSON.stringify(order);
    YB.client.send(data, true);
  };

  // Re-span and re-colour the temperature gauges after a units change.  c3
  // keeps min/max and threshold values in its internal config, so we poke them
  // there and regenerate the level colour rather than rebuilding the gauge.
  // The sensor config getters already report the new units, so we read bounds
  // straight off it.  Per-gauge guards also cover MFD mode (no gauges created).
  SensorGauges.prototype.updateTemperatureGauges = function () {
    const cfg = this.bom.sensorConfig;

    if (this.motorTemperatureGauge) {
      this.motorTemperatureGauge.internal.config.gauge_min = cfg.motor_temperature.min;
      this.motorTemperatureGauge.internal.config.gauge_max = cfg.motor_temperature.max;
      this.motorTemperatureGauge.internal.config.color_threshold.values = cfg.motor_temperature.thresholds;
      this.motorTemperatureGauge.internal.levelColor = this.motorTemperatureGauge.internal.generateLevelColor.call(this.motorTemperatureGauge.internal);
    }

    if (this.waterTemperatureGauge) {
      this.waterTemperatureGauge.internal.config.gauge_min = cfg.water_temperature.min;
      this.waterTemperatureGauge.internal.config.gauge_max = cfg.water_temperature.max;
      this.waterTemperatureGauge.internal.config.color_threshold.values = cfg.water_temperature.thresholds;
      this.waterTemperatureGauge.internal.levelColor = this.waterTemperatureGauge.internal.generateLevelColor.call(this.waterTemperatureGauge.internal);
    }
  };

  SensorGauges.prototype.updatePressureGauges = function () {
    const cfg = this.bom.sensorConfig;

    if (this.membranePressureGauge) {
      this.membranePressureGauge.internal.config.gauge_min = cfg.membrane_pressure.min;
      this.membranePressureGauge.internal.config.gauge_max = cfg.membrane_pressure.max;
      this.membranePressureGauge.internal.config.color_threshold.values = cfg.membrane_pressure.thresholds;
      this.membranePressureGauge.internal.levelColor = this.membranePressureGauge.internal.generateLevelColor.call(this.membranePressureGauge.internal);
    }

    if (this.filterPressureGauge) {
      this.filterPressureGauge.internal.config.gauge_min = cfg.filter_pressure.min;
      this.filterPressureGauge.internal.config.gauge_max = cfg.filter_pressure.max;
      this.filterPressureGauge.internal.config.color_threshold.values = cfg.filter_pressure.thresholds;
      this.filterPressureGauge.internal.levelColor = this.filterPressureGauge.internal.generateLevelColor.call(this.filterPressureGauge.internal);
    }
  };

  SensorGauges.prototype.updateFlowrateGauges = function () {
    const cfg = this.bom.sensorConfig;

    if (this.productFlowrateGauge) {
      this.productFlowrateGauge.internal.config.gauge_min = cfg.product_flowrate.min;
      this.productFlowrateGauge.internal.config.gauge_max = cfg.product_flowrate.max;
      this.productFlowrateGauge.internal.config.color_threshold.values = cfg.product_flowrate.thresholds;
      this.productFlowrateGauge.internal.levelColor = this.productFlowrateGauge.internal.generateLevelColor.call(this.productFlowrateGauge.internal);
    }

    if (this.brineFlowrateGauge) {
      this.brineFlowrateGauge.internal.config.gauge_min = cfg.brine_flowrate.min;
      this.brineFlowrateGauge.internal.config.gauge_max = cfg.brine_flowrate.max;
      this.brineFlowrateGauge.internal.config.color_threshold.values = cfg.brine_flowrate.thresholds;
      this.brineFlowrateGauge.internal.levelColor = this.brineFlowrateGauge.internal.generateLevelColor.call(this.brineFlowrateGauge.internal);
    }

    if (this.totalFlowrateGauge) {
      this.totalFlowrateGauge.internal.config.gauge_min = cfg.total_flowrate.min;
      this.totalFlowrateGauge.internal.config.gauge_max = cfg.total_flowrate.max;
      this.totalFlowrateGauge.internal.config.color_threshold.values = cfg.total_flowrate.thresholds;
      this.totalFlowrateGauge.internal.levelColor = this.totalFlowrateGauge.internal.generateLevelColor.call(this.totalFlowrateGauge.internal);
    }
  };

  YB.SensorGauges = SensorGauges;

  global.YB = YB; // <-- this line makes it global

})(this); // private scope
