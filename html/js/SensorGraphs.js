(function (global) { // private scope
  // work in the global YB namespace.
  var YB = global.YB || {};

  //
  // Graphs page: historical + live sensor data.
  //
  // Each entry in setup describes one tab: which sensors it plots, how
  // to convert their raw firmware values (always metric) into the user's
  // display units, and the y-axis labelling.  Everything downstream (HTML,
  // charts, history fetch, live updates) is generated from this table, so
  // sensors that aren't present on a given board simply drop out.
  //
  // Owns the graphs page entirely; a single instance lives on the Brineomatic
  // object (YB.bom.graphs) and reaches back through this.bom for unit
  // conversions and short-unit labels.
  //
  function SensorGraphs(bom) {
    this.bom = bom;

    this.setup = null;
    this.charts = null;
    this.data = null;
    this.historyLoaded = null;

    // How far back the graphs show, in seconds; driven by the range dropdown.
    this.rangeSeconds = 1 * 3600;
  }

  SensorGraphs.prototype.MAX_POINTS = 20000;
  SensorGraphs.prototype.HEIGHT = 400;
  SensorGraphs.prototype.COLORS = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728'];

  SensorGraphs.prototype.buildSetup = function () {
    const cfg = YB.config.brineomatic;
    const bom = this.bom;

    const hasMotorTemp = cfg.motor_temperature_sensor_type && cfg.motor_temperature_sensor_type != "NONE";
    const hasWaterTemp = cfg.water_temperature_sensor_type && cfg.water_temperature_sensor_type != "NONE";
    const hasTankLevel = cfg.tank_level_sensor_type && cfg.tank_level_sensor_type != "NONE";
    const hasBatteryLevel = cfg.battery_level_sensor_type && cfg.battery_level_sensor_type != "NONE";

    this.setup = [
      {
        key: 'pressure',
        label: 'Pressure',
        axisLabel: `Pressure (${bom.getShortPressureUnits(cfg.pressure_units)})`,
        unit: bom.getShortPressureUnits(cfg.pressure_units),
        decimals: 1,
        series: [
          { sensor: 'filter_pressure', label: 'Filter Pressure', enabled: !!cfg.has_filter_pressure_sensor, convert: v => bom.convertPressure(v, "Bar", cfg.pressure_units) },
          { sensor: 'membrane_pressure', label: 'Membrane Pressure', enabled: !!cfg.has_membrane_pressure_sensor, convert: v => bom.convertPressure(v, "Bar", cfg.pressure_units) },
        ]
      },
      {
        key: 'salinity',
        label: 'Salinity',
        axisLabel: 'Salinity (PPM)',
        unit: 'PPM',
        decimals: 0,
        series: [
          { sensor: 'product_salinity', label: 'Product Salinity', enabled: !!cfg.has_product_tds_sensor, convert: v => v },
          { sensor: 'brine_salinity', label: 'Brine Salinity', enabled: !!cfg.has_brine_tds_sensor, convert: v => v },
        ]
      },
      {
        key: 'flowrate',
        label: 'Flowrate',
        axisLabel: `Flowrate (${bom.getShortFlowrateUnits(cfg.flowrate_units)})`,
        unit: bom.getShortFlowrateUnits(cfg.flowrate_units),
        decimals: 1,
        series: [
          { sensor: 'product_flowrate', label: 'Product Flowrate', enabled: !!cfg.has_product_flow_sensor, convert: v => bom.convertFlowrate(v, "lph", cfg.flowrate_units) },
          { sensor: 'brine_flowrate', label: 'Brine Flowrate', enabled: !!cfg.has_brine_flow_sensor, convert: v => bom.convertFlowrate(v, "lph", cfg.flowrate_units) },
        ]
      },
      {
        key: 'temperature',
        label: 'Temperature',
        axisLabel: `Temperature (°${bom.getShortTemperatureUnits(cfg.temperature_units)})`,
        unit: `°${bom.getShortTemperatureUnits(cfg.temperature_units)}`,
        decimals: 1,
        series: [
          { sensor: 'water_temperature', label: 'Water Temperature', enabled: hasWaterTemp, convert: v => bom.convertTemperature(v, "C", cfg.temperature_units) },
          { sensor: 'motor_temperature', label: 'Motor Temperature', enabled: hasMotorTemp, convert: v => bom.convertTemperature(v, "C", cfg.temperature_units) },
        ]
      },
      {
        key: 'tankLevel',
        label: 'Tank Level',
        axisLabel: 'Tank Level (%)',
        unit: '%',
        decimals: 0,
        series: [
          { sensor: 'tank_level', label: 'Tank Level', enabled: hasTankLevel, skipNegative: true, convert: v => v * 100 },
        ]
      },
      {
        key: 'batteryLevel',
        label: 'Battery',
        axisLabel: 'Battery Level (%)',
        unit: '%',
        decimals: 0,
        series: [
          { sensor: 'battery_level', label: 'Battery Level', enabled: hasBatteryLevel, skipNegative: true, convert: v => v * 100 },
        ]
      },
    ];

    // only show tabs that have at least one available sensor, and let each
    // series inherit its tab's display precision so values can be rounded as
    // they're stored (a steady-state reading then graphs as a flat line
    // instead of jittering in the noise below the displayed precision).
    //
    // The y-axis range is pulled from the gauges (YB.bom.gaugeSetup) so the
    // graphs and the home-page gauges share a single source of truth.  Both
    // store their ranges in the same display units the series plot in, so the
    // gauge bounds drop straight onto each series.  The range is carried
    // per-series (not aggregated onto the tab) so the y-axis can be re-spanned
    // from just the series the user currently has shown — see create().
    const gauges = (this.bom && this.bom.gaugeSetup) || {};
    for (let tab of this.setup) {
      tab.enabled = tab.series.some(s => s.enabled);
      for (let s of tab.series) {
        s.decimals = tab.decimals;
        const g = gauges[s.sensor];
        if (g && g.min !== undefined) {
          s.min = g.min;
          s.max = g.max;
        }
      }
    }
  };

  SensorGraphs.prototype.generateUI = function () {
    let tabs = '';
    let panes = '';
    let first = true;

    for (let tab of this.setup) {
      if (!tab.enabled)
        continue;

      tabs += `
        <button class="btn ${first ? 'btn-primary active' : 'btn-secondary'} bomGraphTab" id="bomGraphTab-${tab.key}" data-bs-toggle="tab"
          data-bs-target="#bomGraphPanel-${tab.key}" type="button" role="tab">${tab.label}</button>`;

      panes += `
        <div class="tab-pane${first ? ' active' : ''}" id="bomGraphPanel-${tab.key}" role="tabpanel" tabindex="0">
          <div id="bomGraphChart-${tab.key}" class="mt-3"></div>
        </div>`;

      first = false;
    }

    let rangeOptions = '';
    for (const m of [5, 10, 15, 30]) {
      const secs = m * 60;
      const selected = (secs === this.rangeSeconds) ? ' selected' : '';
      rangeOptions += `<option value="${secs}"${selected}>Last ${m} Minutes</option>`;
    }
    for (const h of [1, 2, 3, 6, 12]) {
      const secs = h * 3600;
      const selected = (secs === this.rangeSeconds) ? ' selected' : '';
      rangeOptions += `<option value="${secs}"${selected}>Last ${h} Hour${h > 1 ? 's' : ''}</option>`;
    }

    return `
      <div id="bomGraphs" class="col-md-12">
        <div class="row align-items-center">
          <div class="col-12 col-md-8">
            <div class="nav" id="bomGraphsTabs" role="tablist">${tabs}</div>
          </div>
          <div class="col-12 col-md-4">
            <div class="my-2 form-floating">
              <select id="bomGraphRange" class="form-select" aria-label="Graph time range">${rangeOptions}</select>
              <label for="bomGraphRange">Time Range</label>
            </div>
          </div>
        </div>
        <div class="tab-content">${panes}</div>
      </div>`;
  };

  // uPlot needs an explicit pixel width.  All tab panes share the same width
  // (the col-md-12 container), and that container is visible even while an
  // individual pane is hidden, so measure it there instead of the pane.
  SensorGraphs.prototype.width = function () {
    const el = document.getElementById('bomGraphs');
    return (el && el.clientWidth) ? el.clientWidth : 800;
  };

  SensorGraphs.prototype.create = function () {
    if (this.charts)
      return;

    const self = this;

    this.charts = {};
    this.data = {};
    this.historyLoaded = {};

    const width = this.width();

    // uPlot draws axis labels/ticks/grid onto the canvas, so unlike c3's SVG
    // text they can't be recoloured from CSS.  The label/tick text colour flips
    // with light/dark mode, so we read it live into this._labelColor and hand
    // uPlot a function (re-evaluated on every redraw) rather than a fixed value;
    // the MutationObserver at the end of create() repaints when the theme flips.
    this.refreshThemeColor();
    // A translucent grey reads as a faint gridline on both the light and dark
    // theme backgrounds; --bs-border-color renders too dark/strong in light mode.
    const gridColor = 'rgba(128,128,128,0.25)';

    for (let tab of this.setup) {
      if (!tab.enabled)
        continue;

      // uPlot draws every series against one shared x axis, so the first
      // series entry is the (empty) x config and the rest are the value
      // series in the same order dataFor() emits their columns.
      const series = [{}];
      const enabled = tab.series.filter(s => s.enabled);
      for (let i = 0; i < enabled.length; i++) {
        const s = enabled[i];

        this.data[s.sensor] = { t: [], v: [], _ema: undefined };

        series.push({
          label: s.label,
          stroke: this.COLORS[i % this.COLORS.length],
          width: 1.5,
          // sensors sample independently, so a series only has values at its
          // own timestamps and is null at the others — span those gaps so the
          // line stays continuous instead of breaking at every other point.
          spanGaps: true,
          points: { show: false },
          value: function (u, value) {
            if (value === null || value === undefined || isNaN(value))
              return '--';
            const rounded = Number(value).toFixed(tab.decimals);
            const sep = (tab.unit === '%') ? '' : ' ';
            return tab.unit ? `${rounded}${sep}${tab.unit}` : rounded;
          }
        });
      }

      const opts = {
        width: width,
        height: this.HEIGHT,
        series: series,
        scales: {
          x: { time: true },
          y: {
            // Span the y-axis across only the series the user currently has
            // shown (uPlot toggles series.show when their legend label is
            // clicked), so hiding the high-range line lets the low-range one
            // fill the chart.  Each series carries its gauge range; the widest
            // of the shown series wins, matching the gauge bounds.  uPlot's
            // first series entry is the x axis, so enabled[i] is u.series[i+1].
            range: function (u, dataMin, dataMax) {
              let cMin, cMax;
              for (let i = 0; i < enabled.length; i++) {
                const us = u.series[i + 1];
                if (us && us.show && enabled[i].min !== undefined) {
                  cMin = (cMin === undefined) ? enabled[i].min : Math.min(cMin, enabled[i].min);
                  cMax = (cMax === undefined) ? enabled[i].max : Math.max(cMax, enabled[i].max);
                }
              }
              if (dataMin == null || dataMax == null)
                return [cMin !== undefined ? cMin : 0, cMax !== undefined ? cMax : 1];
              let min = dataMin, max = dataMax;
              const pad = (max - min) * 0.1 || Math.abs(max) * 0.1 || 1;
              min -= pad;
              max += pad;
              if (cMin !== undefined) min = cMin;
              if (cMax !== undefined) max = cMax;
              return [min, max];
            }
          }
        },
        axes: [
          {
            stroke: () => self._labelColor,
            grid: { stroke: gridColor, width: 1 },
            ticks: { stroke: gridColor, width: 1 },
            values: function (u, splits) {
              return splits.map(function (t) {
                const d = new Date(t * 1000);
                const p = n => String(n).padStart(2, '0');
                return `${p(d.getHours())}:${p(d.getMinutes())}`;
              });
            }
          },
          {
            label: tab.axisLabel,
            stroke: () => self._labelColor,
            grid: { stroke: gridColor, width: 1 },
            ticks: { stroke: gridColor, width: 1 }
          }
        ],
        hooks: {
          // Track whether the user has zoomed in.  A drag-select is uPlot's
          // zoom gesture, so flag it; double-click is uPlot's reset-to-full,
          // so clear it.  refresh() uses this to avoid snapping the view
          // back to the full range every time a live point arrives.
          setSelect: [
            function (u) {
              if (u.select.width > 0)
                u._userZoomed = true;
            }
          ],
          init: [
            function (u) {
              u.over.addEventListener('dblclick', function () {
                u._userZoomed = false;
              });
            }
          ]
        }
      };

      this.charts[tab.key] = new uPlot(opts, this.dataFor(tab), document.getElementById(`bomGraphChart-${tab.key}`));
    }

    // uPlot is created at zero width inside hidden tab panes; resize on reveal
    $('#bomGraphsTabs button[data-bs-toggle="tab"]').on('shown.bs.tab', function (e) {
      // Bootstrap's tab plugin only toggles .active; recolour the pills so the
      // selected one is primary and the rest fall back to secondary.
      $('#bomGraphsTabs .bomGraphTab').removeClass('btn-primary').addClass('btn-secondary');
      $(e.target).removeClass('btn-secondary').addClass('btn-primary');

      const key = e.target.id.replace('bomGraphTab-', '');
      if (self.charts && self.charts[key])
        self.charts[key].setSize({ width: self.width(), height: self.HEIGHT });
      // fetch this tab's history on first reveal (no-op if already loaded)
      self.loadHistory(self.setup.find(tab => tab.key === key));
    });

    // changing the time range refetches fresh data for the new window
    $('#bomGraphRange').on('change', function () {
      self.setRange(parseInt(this.value, 10));
    });

    // keep the charts fitted to the window; setSize preserves the current
    // zoom, and we debounce so a drag-resize doesn't redraw on every pixel
    let resizeTimer;
    $(window).on('resize.bomGraphs', function () {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(function () {
        const w = self.width();
        for (let key in self.charts)
          self.charts[key].setSize({ width: w, height: self.HEIGHT });
      }, 150);
    });

    // The theme toggle flips data-bs-theme on <html>, which swaps the CSS
    // colour variables but doesn't notify uPlot.  Watch that attribute, re-read
    // the label colour, and redraw so the axis text recolours immediately.
    this._themeObserver = new MutationObserver(function () {
      self.refreshThemeColor();
      for (let key in self.charts)
        self.charts[key].redraw(false, true);
    });
    this._themeObserver.observe(document.documentElement, { attributes: true, attributeFilter: ['data-bs-theme'] });
  };

  // Read the active theme's body colour (flips with light/dark mode) for use as
  // the axis label/tick text colour.  Stored on the instance so the stroke
  // functions handed to uPlot pick up the current value on every redraw.
  SensorGraphs.prototype.refreshThemeColor = function () {
    const css = getComputedStyle(document.documentElement);
    this._labelColor = css.getPropertyValue('--bs-body-color').trim() || '#000';
  };

  // Called when the graphs page opens.  Config may not have arrived yet (the
  // page stays hidden until it's ready) and the container is only revealed
  // once the page is ready, so wait for both before creating the charts —
  // uPlot needs a real container width to size itself.
  SensorGraphs.prototype.open = function () {
    const self = this;

    if (!this.setup || !$('#graphsPage').is(':visible')) {
      // retry until ready, but stop if the user navigates away.  the check
      // lives inside the timeout because onOpen callbacks fire before
      // App.currentPage has been updated to "graphs".
      setTimeout(() => {
        if (YB.App.currentPage == "graphs")
          self.open();
      }, 100);
      return;
    }

    this.create();
    this.loadHistory(this.activeTab());
    const width = this.width();
    for (let key in this.charts)
      this.charts[key].setSize({ width: width, height: this.HEIGHT });
  };

  // Lazily fetch one tab's history the first time it's viewed.  Fetching every
  // tab up front pulls a lot of binary blobs the user may never look at, so we
  // only load the tab that's actually on screen and remember which we've done
  // (historyLoaded) so switching back and forth doesn't refetch.
  //
  // Each fetch parses into a fresh buffer off to the side; the live data is
  // swapped only once every series has arrived, in one shot, so the chart
  // never redraws against a half-loaded mix of old and new windows (live
  // updates keep redrawing the old, consistent data in the meantime).
  SensorGraphs.prototype.loadHistory = function (tab) {
    const self = this;

    if (!tab || !tab.enabled || this.historyLoaded[tab.key])
      return;

    this.historyLoaded[tab.key] = true;

    // setRange() bumps _loadSeq, so a range change mid-flight makes this load
    // stale: its results are dropped rather than clobbering the newer fetch.
    const seq = this._loadSeq || 0;

    const enabledSeries = tab.series.filter(s => s.enabled);
    Promise.all(enabledSeries.map(s => self.fetchHistory(s)))
      .then(results => {
        if (seq !== (self._loadSeq || 0))
          return; // superseded by a range change; the newer load will refresh
        enabledSeries.forEach((s, i) => self.data[s.sensor] = results[i]);
        self.refresh(tab);
      })
      .catch(err => {
        // let a failed load retry the next time the tab is shown
        self.historyLoaded[tab.key] = false;
        YB.log(`sensor history load failed: ${err}`);
      });
  };

  // Switch the history window.  Drop the loaded-flags so every tab refetches
  // against the new range; the visible tab reloads now and the rest reload
  // lazily the next time they're shown.  Bumping _loadSeq invalidates any
  // load still in flight so its (old-range) data can't land after ours.
  SensorGraphs.prototype.setRange = function (rangeSeconds) {
    this.rangeSeconds = rangeSeconds;
    this._loadSeq = (this._loadSeq || 0) + 1;
    this.historyLoaded = {};
    const tab = this.activeTab();
    if (tab)
      this.loadHistory(tab);
  };

  // Find the tab whose pane is currently active (the one the user is viewing).
  SensorGraphs.prototype.activeTab = function () {
    return this.setup.find(tab =>
      tab.enabled && $(`#bomGraphPanel-${tab.key}`).hasClass('active'));
  };

  // Pull one sensor's history as a raw binary blob and unpack it into the
  // series' data arrays.  Format: 16-byte preamble (magic "BOMH", version,
  // point size, device uptime seconds, point count) followed by packed
  // 8-byte points of uint32 uptime seconds + float32 value, little-endian.
  // Timestamps are device uptime, so we anchor them to the browser clock:
  // wall time = now - (device uptime now - point uptime).
  SensorGraphs.prototype.fetchHistory = function (series) {
    const self = this;
    const clientNow = Date.now();

    let url = `/api/sensor_history?sensor=${series.sensor}`;
    // The firmware filters on seconds-before-now, so the window is just the
    // selected range expressed that way — no boot anchor needed, so this works
    // on the very first fetch too.  Without a range we pull the whole buffer.
    if (self.rangeSeconds)
      url += `&startTime=${Math.round(self.rangeSeconds)}`;

    return fetch(url)
      .then(response => {
        if (!response.ok)
          throw new Error(`HTTP ${response.status}`);
        return response.arrayBuffer();
      })
      .then(buffer => {
        const dv = new DataView(buffer);
        if (buffer.byteLength < 16 || dv.getUint32(0, true) != 0x484D4F42)
          throw new Error(`bad history preamble for ${series.sensor}`);

        const pointSize = dv.getUint16(6, true);
        const uptime = dv.getUint32(8, true);
        const count = Math.min(dv.getUint32(12, true), Math.floor((buffer.byteLength - 16) / pointSize));

        console.log(`${series.sensor} history size: ${count}`);

        // trim anything older than the window here as a backstop against slack
        // in the firmware's filter (and rounding at the window edge).
        const minWall = self.rangeSeconds ? clientNow / 1000 - self.rangeSeconds : -Infinity;

        // parse into a fresh buffer rather than the live arrays; loadHistory
        // swaps it in once every series has arrived, so a slow fetch never
        // redraws against half-loaded data.  Smoothing restarts here and the
        // EMA state rides along into live updates after the swap.
        const data = { t: [], v: [], _ema: undefined };

        for (let i = 0; i < count; i++) {
          const offset = 16 + i * pointSize;
          const time = dv.getUint32(offset, true);
          const value = dv.getFloat32(offset + 4, true);

          if (isNaN(value) || (series.skipNegative && value < 0))
            continue;

          // uPlot's time scale wants seconds, so anchor to the browser clock in seconds
          const wall = clientNow / 1000 - (uptime - time);
          if (wall < minWall)
            continue;

          data.t.push(wall);
          data.v.push(self.smoothValue(series, series.convert(value), data));
        }

        return data;
      });
  };

  // Smooth (optionally) and round an incoming value before it's stored.
  //
  // series.smooth is the EMA alpha (0..1): lower = smoother/laggier.  The EMA
  // state lives on the passed data object (the same one the value is stored
  // in) so the smoothed value carries from loaded history into live updates
  // seamlessly; we keep it at full precision and only round the returned
  // (stored/plotted) value to series.decimals so a steady reading graphs as a
  // flat line instead of jittering in sub-display noise.
  SensorGraphs.prototype.smoothValue = function (series, value, data) {
    if (series.smooth) {
      const prev = data._ema;
      value = (prev === undefined) ? value : prev + series.smooth * (value - prev);
      data._ema = value;
    }
    return (series.decimals === undefined) ? value : Number(value.toFixed(series.decimals));
  };

  // Build uPlot's aligned data array for a tab: [xs, valuesForSeries1, ...].
  // uPlot plots all series against one shared x axis, but each sensor samples
  // independently on its own timestamps, so we merge every enabled series'
  // timestamps into a single sorted x array and fill each series' column with
  // its value where it has a sample and null elsewhere (series.spanGaps keeps
  // the lines continuous across those nulls).  An all-empty tab yields [[], []]
  // — uPlot renders an empty plot without complaint.
  SensorGraphs.prototype.dataFor = function (tab) {
    const list = tab.series.filter(s => s.enabled).map(s => this.data[s.sensor]);
    const idx = list.map(() => 0);
    const xs = [];
    const cols = list.map(() => []);

    for (; ;) {
      let minT = Infinity;
      for (let i = 0; i < list.length; i++)
        if (idx[i] < list[i].t.length && list[i].t[idx[i]] < minT)
          minT = list[i].t[idx[i]];

      if (minT === Infinity)
        break;

      xs.push(minT);
      for (let i = 0; i < list.length; i++) {
        if (idx[i] < list[i].t.length && list[i].t[idx[i]] === minT) {
          cols[i].push(list[i].v[idx[i]]);
          idx[i]++;
        } else {
          cols[i].push(null);
        }
      }
    }

    return [xs, ...cols];
  };

  SensorGraphs.prototype.refresh = function (tab) {
    const u = this.charts && this.charts[tab.key];
    if (!u)
      return;

    // setData's second arg is resetScales: skip the auto-refit while the user
    // is zoomed in so their view stays put as new points stream in.  When not
    // zoomed we let it refit so the chart keeps following the latest data.
    u.setData(this.dataFor(tab), !u._userZoomed);
  };

  // Append realtime values from an update message to the graph data, matching
  // the firmware's 1Hz sample rate, and redraw the chart the user is viewing.
  SensorGraphs.prototype.update = function (msg) {
    if (!this.charts)
      return;

    const nowSec = Date.now() / 1000;

    for (let tab of this.setup) {
      if (!tab.enabled)
        continue;

      let changed = false;
      for (let s of tab.series) {
        if (!s.enabled || msg[s.sensor] === undefined)
          continue;

        const value = parseFloat(msg[s.sensor]);
        if (isNaN(value) || (s.skipNegative && value < 0))
          continue;

        const data = this.data[s.sensor];
        const lastTime = data.t.length ? data.t[data.t.length - 1] : 0;
        if (nowSec - lastTime < 1)
          continue;

        data.t.push(nowSec);
        data.v.push(this.smoothValue(s, s.convert(value), data));

        // age out points that fall off the back of the selected window so the
        // graph keeps showing exactly the chosen range as live data streams in
        const minT = this.rangeSeconds ? nowSec - this.rangeSeconds : 0;
        while (data.t.length > this.MAX_POINTS || (data.t.length && data.t[0] < minT)) {
          data.t.shift();
          data.v.shift();
        }
        changed = true;
      }

      // only redraw the graph the user is actually looking at
      if (changed && YB.App.currentPage == "graphs" && $(`#bomGraphPanel-${tab.key}`).hasClass('active'))
        this.refresh(tab);
    }
  };

  YB.SensorGraphs = SensorGraphs;

  global.YB = YB; // <-- this line makes it global

})(this); // private scope
