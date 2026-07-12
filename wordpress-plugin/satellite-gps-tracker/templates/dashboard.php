<?php
/**
 * Dashboard markup, shared by the admin page and the [satgps_dashboard] shortcode.
 * The JavaScript in assets/js/satgps-dashboard.js populates every [data-field].
 *
 * @package SatelliteGpsTracker
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}
?>
<div id="satgps-app" class="satgps-app">

	<!-- Toolbar -->
	<div class="satgps-toolbar">
		<div class="satgps-toolbar-left">
			<span class="satgps-stat" id="satgps-link-stat">
				<span class="satgps-status-dot" id="satgps-status-dot" aria-hidden="true"></span>
				<span class="satgps-status-text" id="satgps-status-text"><?php esc_html_e( 'Connecting…', 'satgps' ); ?></span>
			</span>
			<span class="satgps-stat" id="satgps-gps-stat">
				<span class="satgps-status-dot" id="satgps-gps-dot" aria-hidden="true"></span>
				<span class="satgps-status-text" id="satgps-gps-text">GPS —</span>
			</span>
		</div>
		<div class="satgps-toolbar-right">
			<label class="satgps-inline">
				<?php esc_html_e( 'Device', 'satgps' ); ?>
				<select id="satgps-device"></select>
			</label>
			<label class="satgps-inline">
				<?php esc_html_e( 'Range', 'satgps' ); ?>
				<select id="satgps-range">
					<option value="1h"><?php esc_html_e( 'Last hour', 'satgps' ); ?></option>
					<option value="6h"><?php esc_html_e( 'Last 6 hours', 'satgps' ); ?></option>
					<option value="24h" selected><?php esc_html_e( 'Last 24 hours', 'satgps' ); ?></option>
					<option value="7d"><?php esc_html_e( 'Last 7 days', 'satgps' ); ?></option>
					<option value="30d"><?php esc_html_e( 'Last 30 days', 'satgps' ); ?></option>
				</select>
			</label>
			<label class="satgps-inline satgps-live-toggle">
				<input type="checkbox" id="satgps-live" checked />
				<?php esc_html_e( 'Live', 'satgps' ); ?>
			</label>
			<span class="satgps-export">
				<span class="satgps-export-label"><?php esc_html_e( 'Export', 'satgps' ); ?></span>
				<button type="button" class="satgps-export-btn" id="satgps-export-csv" title="<?php esc_attr_e( 'Download the selected range as CSV', 'satgps' ); ?>">CSV</button>
				<button type="button" class="satgps-export-btn" id="satgps-export-gpx" title="<?php esc_attr_e( 'Download the selected range as a GPX track', 'satgps' ); ?>">GPX</button>
			</span>
		</div>
	</div>

	<!-- Live stat tiles -->
	<div class="satgps-tiles">
		<div class="satgps-tile" data-tile="fix">
			<div class="satgps-tile-label"><?php esc_html_e( 'Fix', 'satgps' ); ?></div>
			<div class="satgps-tile-value" data-field="fix">—</div>
			<div class="satgps-tile-sub" data-field="fix_sub"></div>
		</div>
		<div class="satgps-tile">
			<div class="satgps-tile-label"><?php esc_html_e( 'Satellites', 'satgps' ); ?></div>
			<div class="satgps-tile-value" data-field="sats_used">—</div>
			<div class="satgps-tile-sub"><?php esc_html_e( 'used /', 'satgps' ); ?> <span data-field="sats_in_view">—</span> <?php esc_html_e( 'in view', 'satgps' ); ?></div>
		</div>
		<div class="satgps-tile">
			<div class="satgps-tile-label"><?php esc_html_e( 'Speed', 'satgps' ); ?></div>
			<div class="satgps-tile-value" data-field="speed">—</div>
			<div class="satgps-tile-sub" data-field="speed_unit"></div>
		</div>
		<div class="satgps-tile">
			<div class="satgps-tile-label"><?php esc_html_e( 'Altitude', 'satgps' ); ?></div>
			<div class="satgps-tile-value" data-field="alt">—</div>
			<div class="satgps-tile-sub" data-field="alt_sub">m <?php esc_html_e( 'above sea level', 'satgps' ); ?></div>
		</div>
		<div class="satgps-tile">
			<div class="satgps-tile-label"><?php esc_html_e( 'Accuracy', 'satgps' ); ?></div>
			<div class="satgps-tile-value" data-field="acc">—</div>
			<div class="satgps-tile-sub" data-field="acc_sub"></div>
		</div>
		<div class="satgps-tile">
			<div class="satgps-tile-label"><?php esc_html_e( 'Heading', 'satgps' ); ?></div>
			<div class="satgps-tile-value" data-field="heading">—</div>
			<div class="satgps-tile-sub" data-field="heading_cardinal"></div>
		</div>
	</div>

	<!-- Map + sky plot -->
	<div class="satgps-grid-2">
		<?php
		// [satgps_dashboard hide_map="1" hide_map_message="…"] hides the map + coordinates
		// (e.g. to embed the dashboard publicly without revealing exact location).
		// $atts is only set in the shortcode context; the admin dashboard always shows the map.
		$satgps_hide_map = isset( $atts ) && is_array( $atts ) && isset( $atts['hide_map'] )
			&& in_array( strtolower( trim( (string) $atts['hide_map'] ) ), array( '1', 'yes', 'true', 'on' ), true );
		$satgps_map_msg  = ( isset( $atts ) && is_array( $atts ) && ! empty( $atts['hide_map_message'] ) )
			? trim( (string) $atts['hide_map_message'] )
			: __( 'Live location map is hidden.', 'satgps' );
		?>
		<section class="satgps-card">
			<h2><?php esc_html_e( 'Position &amp; track', 'satgps' ); ?></h2>
			<?php if ( $satgps_hide_map ) : ?>
				<div class="satgps-map-hidden">
					<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="4" y="10.5" width="16" height="10" rx="2"/><path d="M8 10.5V7a4 4 0 0 1 8 0v3.5"/></svg>
					<span><?php echo esc_html( $satgps_map_msg ); ?></span>
				</div>
			<?php else : ?>
				<div id="satgps-map" class="satgps-map"></div>
				<div class="satgps-coords">
					<span data-field="latlon">—</span>
					<span class="satgps-muted" data-field="last_ts"></span>
				</div>
			<?php endif; ?>
		</section>

		<section class="satgps-card">
			<h2><?php esc_html_e( 'Sky plot', 'satgps' ); ?>
				<button type="button" class="satgps-help" data-help="skyplot" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button>
			</h2>
			<canvas id="satgps-skyplot" width="320" height="320" class="satgps-skyplot"></canvas>
			<div class="satgps-legend" id="satgps-sky-legend"></div>
		</section>
	</div>

	<!-- Signal + constellation -->
	<div class="satgps-grid-2">
		<section class="satgps-card">
			<h2><?php esc_html_e( 'Signal strength (C/N₀)', 'satgps' ); ?>
				<button type="button" class="satgps-help" data-help="snr" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button>
			</h2>
			<div class="satgps-chart-wrap"><canvas id="satgps-snr"></canvas></div>
		</section>
		<section class="satgps-card">
			<h2><?php esc_html_e( 'Constellations in view', 'satgps' ); ?></h2>
			<div class="satgps-const" id="satgps-const"></div>
		</section>
	</div>

	<!-- Receiver health & integrity -->
	<section class="satgps-card">
		<h2><?php esc_html_e( 'Receiver health &amp; integrity', 'satgps' ); ?>
			<button type="button" class="satgps-help" data-help="rf" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button>
			<span class="satgps-tile-value satgps-health-chip is-muted" data-field="integrity" style="font-size:14px;font-weight:700;margin-left:auto"></span>
		</h2>
		<div class="satgps-health-key" aria-hidden="true">
			<span><i style="background:var(--satgps-muted)"></i><?php esc_html_e( 'not measured', 'satgps' ); ?></span>
			<span><i style="background:var(--satgps-ok)"></i><?php esc_html_e( 'healthy', 'satgps' ); ?></span>
			<span><i style="background:var(--satgps-warn)"></i><?php esc_html_e( 'watch', 'satgps' ); ?></span>
			<span><i style="background:var(--satgps-bad)"></i><?php esc_html_e( 'problem', 'satgps' ); ?></span>
		</div>
		<div class="satgps-tiles satgps-tiles-compact">
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Interference', 'satgps' ); ?><button type="button" class="satgps-help satgps-help-sm" data-help="jam" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button></div><div class="satgps-tile-value is-muted" data-field="jam">—</div><div class="satgps-tile-sub" data-field="jam_sub"><?php esc_html_e( 'RF jamming monitor', 'satgps' ); ?></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'RF gain (AGC)', 'satgps' ); ?><button type="button" class="satgps-help satgps-help-sm" data-help="agc" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button></div><div class="satgps-tile-value is-muted" data-field="agc">—</div><div class="satgps-tile-sub" data-field="agc_sub"><?php esc_html_e( 'front-end gain', 'satgps' ); ?></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Noise floor', 'satgps' ); ?><button type="button" class="satgps-help satgps-help-sm" data-help="noise" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button></div><div class="satgps-tile-value is-muted" data-field="noise">—</div><div class="satgps-tile-sub" data-field="noise_sub"><?php esc_html_e( 'broadband level', 'satgps' ); ?></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Antenna feed', 'satgps' ); ?><button type="button" class="satgps-help satgps-help-sm" data-help="ant" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button></div><div class="satgps-tile-value is-muted" data-field="ant">—</div><div class="satgps-tile-sub" data-field="ant_sub"><?php esc_html_e( 'feed sense', 'satgps' ); ?></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Spoofing', 'satgps' ); ?><button type="button" class="satgps-help satgps-help-sm" data-help="spoof" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button></div><div class="satgps-tile-value is-muted" data-field="spoof">—</div><div class="satgps-tile-sub" data-field="spoof_sub"><?php esc_html_e( 'authenticity check', 'satgps' ); ?></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Time to first fix', 'satgps' ); ?><button type="button" class="satgps-help satgps-help-sm" data-help="ttff" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button></div><div class="satgps-tile-value is-muted" data-field="ttff">—</div><div class="satgps-tile-sub"><?php esc_html_e( 'at last power-on', 'satgps' ); ?></div></div>
		</div>
	</section>

	<!-- History charts -->
	<div class="satgps-grid-2">
		<section class="satgps-card">
			<h2><?php esc_html_e( 'Speed over time', 'satgps' ); ?></h2>
			<div class="satgps-chart-wrap"><canvas id="satgps-speed-chart"></canvas></div>
		</section>
		<section class="satgps-card">
			<h2><?php esc_html_e( 'Altitude over time', 'satgps' ); ?></h2>
			<div class="satgps-chart-wrap"><canvas id="satgps-alt-chart"></canvas></div>
		</section>
	</div>

	<div class="satgps-grid-2">
		<section class="satgps-card">
			<h2><?php esc_html_e( 'Vertical speed over time', 'satgps' ); ?></h2>
			<div class="satgps-chart-wrap"><canvas id="satgps-vspeed-chart"></canvas></div>
		</section>
		<section class="satgps-card">
			<h2><?php esc_html_e( 'Accuracy over time', 'satgps' ); ?></h2>
			<div class="satgps-chart-wrap"><canvas id="satgps-acc-chart"></canvas></div>
		</section>
	</div>

	<!-- Trip stats -->
	<section class="satgps-card">
		<h2><?php esc_html_e( 'Trip statistics', 'satgps' ); ?> <span class="satgps-muted" data-field="range_label"></span></h2>
		<div class="satgps-tiles satgps-tiles-compact">
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Distance', 'satgps' ); ?></div><div class="satgps-tile-value" data-field="distance">—</div><div class="satgps-tile-sub" data-field="distance_unit"></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Max speed', 'satgps' ); ?></div><div class="satgps-tile-value" data-field="max_speed">—</div><div class="satgps-tile-sub" data-field="max_speed_unit"></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Avg speed', 'satgps' ); ?></div><div class="satgps-tile-value" data-field="avg_speed">—</div><div class="satgps-tile-sub" data-field="avg_speed_unit"></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Moving time', 'satgps' ); ?></div><div class="satgps-tile-value" data-field="moving_time">—</div><div class="satgps-tile-sub"><?php esc_html_e( 'h:m:s', 'satgps' ); ?></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Max altitude', 'satgps' ); ?></div><div class="satgps-tile-value" data-field="max_alt">—</div><div class="satgps-tile-sub">m</div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Data points', 'satgps' ); ?></div><div class="satgps-tile-value" data-field="points">—</div><div class="satgps-tile-sub"><?php esc_html_e( 'fixes', 'satgps' ); ?></div></div>
			<div class="satgps-tile"><div class="satgps-tile-label"><?php esc_html_e( 'Odometer', 'satgps' ); ?><button type="button" class="satgps-help satgps-help-sm" data-help="odo" aria-label="<?php esc_attr_e( 'What is this?', 'satgps' ); ?>">?</button></div><div class="satgps-tile-value" data-field="odo">—</div><div class="satgps-tile-sub" data-field="odo_sub"><?php esc_html_e( 'device · since power-on', 'satgps' ); ?></div></div>
		</div>
	</section>

	<!-- Educational -->
	<section class="satgps-card satgps-learn">
		<h2><?php esc_html_e( 'How this works', 'satgps' ); ?></h2>
		<details>
			<summary><?php esc_html_e( 'What am I looking at?', 'satgps' ); ?></summary>
			<p><?php esc_html_e( 'A GNSS receiver (this one is a u-blox NEO-M9N) listens to radio signals from navigation satellites orbiting ~20,000 km up. By measuring how long each signal takes to arrive, it works out its own position, altitude, speed and heading, then streams that to this site over WiFi.', 'satgps' ); ?></p>
		</details>
		<details>
			<summary><?php esc_html_e( 'The four constellations', 'satgps' ); ?></summary>
			<p><?php esc_html_e( 'GPS (USA), GLONASS (Russia), Galileo (EU) and BeiDou (China) are independent satellite systems. A multi-constellation receiver uses all of them at once, so it sees more satellites and gets a faster, more reliable fix — especially among buildings or trees.', 'satgps' ); ?></p>
		</details>
		<details>
			<summary><?php esc_html_e( 'Sky plot &amp; signal strength', 'satgps' ); ?></summary>
			<p><?php esc_html_e( 'The sky plot shows where each satellite is in the sky: the centre is straight overhead (zenith), the edge is the horizon, and the angle around the circle is the compass bearing. Colour shows signal strength (C/N₀ in dB-Hz) — green is strong, red is weak. Satellites spread evenly across the sky give the most accurate fix.', 'satgps' ); ?></p>
		</details>
		<details>
			<summary><?php esc_html_e( 'What is HDOP / accuracy?', 'satgps' ); ?></summary>
			<p><?php esc_html_e( 'HDOP (Horizontal Dilution of Precision) describes how favourable the satellite geometry is. Lower is better: under 1 is ideal, 1–2 excellent, 2–5 good. A low HDOP with many satellites means the position is trustworthy to within a few metres.', 'satgps' ); ?></p>
		</details>
	</section>

	<p class="satgps-footer satgps-muted">
		<?php esc_html_e( 'Satellite GPS Tracker', 'satgps' ); ?> ·
		<span data-field="device_footer"></span>
	</p>
</div>

<div id="satgps-help-modal" class="satgps-modal" hidden>
	<div class="satgps-modal-box">
		<button type="button" class="satgps-modal-close" aria-label="<?php esc_attr_e( 'Close', 'satgps' ); ?>">&times;</button>
		<div id="satgps-help-content"></div>
	</div>
</div>
