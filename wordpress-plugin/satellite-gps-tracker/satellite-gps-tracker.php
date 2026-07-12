<?php
/**
 * Plugin Name:       Satellite GPS Tracker
 * Plugin URI:        https://github.com/peterjmayhew/Satellite
 * Description:        Receives live GNSS telemetry from an ESP32 satellite receiver and presents it as an interactive map, sky plot, signal charts and trip statistics.
 * Version:           1.4.7
 * Requires at least: 5.8
 * Requires PHP:      7.4
 * Update URI:        https://github.com/peterjmayhew/Satellite
 * Author:            Peter J Mayhew
 * License:           GPL-2.0-or-later
 * Text Domain:       satgps
 *
 * @package SatelliteGpsTracker
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit; // No direct access.
}

define( 'SATGPS_VERSION', '1.4.7' );
define( 'SATGPS_DB_VERSION', '5' );
define( 'SATGPS_FILE', __FILE__ );
define( 'SATGPS_DIR', plugin_dir_path( __FILE__ ) );
define( 'SATGPS_URL', plugin_dir_url( __FILE__ ) );
define( 'SATGPS_REST_NS', 'satgps/v1' );

require_once SATGPS_DIR . 'includes/class-satgps-db.php';
require_once SATGPS_DIR . 'includes/class-satgps-rest.php';
require_once SATGPS_DIR . 'includes/class-satgps-admin.php';
require_once SATGPS_DIR . 'includes/class-satgps-shortcode.php';
require_once SATGPS_DIR . 'includes/class-satgps-plugin.php';

/**
 * GitHub-based automatic updates.
 *
 * Uses the bundled Plugin Update Checker library to serve updates from this
 * plugin's GitHub repository, so they appear on the normal Plugins screen (with
 * the per-plugin auto-update toggle) — no manual zip uploads.
 *
 * Updates are published as GitHub Releases with the built
 * `satellite-gps-tracker.zip` attached as a release asset (see tools/package.py
 * and .github/workflows/release.yml). Because the plugin lives in a subfolder of
 * a monorepo, release-assets mode is required: it installs the attached zip
 * (correct folder structure) instead of GitHub's auto-generated source zip of
 * the whole repository.
 */
function satgps_init_updater() {
	$loader = SATGPS_DIR . 'lib/plugin-update-checker/plugin-update-checker.php';
	if ( ! is_readable( $loader ) ) {
		return;
	}
	require_once $loader;

	if ( ! class_exists( '\YahnisElsts\PluginUpdateChecker\v5\PucFactory' ) ) {
		return;
	}

	$checker = \YahnisElsts\PluginUpdateChecker\v5\PucFactory::buildUpdateChecker(
		'https://github.com/peterjmayhew/Satellite/', // repository URL (not the subfolder)
		SATGPS_FILE,                                   // main plugin file
		'satellite-gps-tracker'                        // slug = plugin folder name
	);

	// GitHub's default branch. Release/tag detection is automatic; the offered
	// version is taken from the release tag (e.g. v1.4.0).
	$checker->setBranch( 'main' );

	// Install the zip attached to each GitHub Release rather than the repo's
	// auto-generated source zip (which would have the wrong folder structure).
	$api = $checker->getVcsApi();
	if ( is_object( $api ) && method_exists( $api, 'enableReleaseAssets' ) ) {
		$api->enableReleaseAssets( '/satellite-gps-tracker\.zip$/i' );
	}
}
satgps_init_updater();

/**
 * Default settings, created on activation and used as a fallback.
 *
 * @return array
 */
function satgps_default_settings() {
	return array(
		'api_key'         => '',
		'allow_public'    => 0,     // Expose read endpoints + shortcode to non-admins.
		'retention_days'  => 90,    // 0 = keep forever.
		'units'           => 'kmh', // 'kmh' or 'mph'.
		'map_zoom'        => 13,
	);
}

/**
 * Activation: create the table and seed settings (incl. a random API key).
 */
function satgps_activate() {
	SatGPS_DB::create_table();

	$settings = get_option( 'satgps_settings' );
	if ( ! is_array( $settings ) ) {
		$settings = satgps_default_settings();
	}
	if ( empty( $settings['api_key'] ) ) {
		$settings['api_key'] = wp_generate_password( 40, false, false );
	}
	update_option( 'satgps_settings', $settings );
	update_option( 'satgps_db_version', SATGPS_DB_VERSION );

	if ( ! wp_next_scheduled( 'satgps_daily_prune' ) ) {
		wp_schedule_event( time() + HOUR_IN_SECONDS, 'daily', 'satgps_daily_prune' );
	}
}
register_activation_hook( __FILE__, 'satgps_activate' );

/**
 * Deactivation: clear the scheduled prune (data + settings are kept).
 */
function satgps_deactivate() {
	$timestamp = wp_next_scheduled( 'satgps_daily_prune' );
	if ( $timestamp ) {
		wp_unschedule_event( $timestamp, 'satgps_daily_prune' );
	}
}
register_deactivation_hook( __FILE__, 'satgps_deactivate' );

/**
 * Daily housekeeping: prune old fixes per the retention setting.
 */
function satgps_run_prune() {
	$settings = get_option( 'satgps_settings', satgps_default_settings() );
	$days     = isset( $settings['retention_days'] ) ? (int) $settings['retention_days'] : 0;
	if ( $days > 0 ) {
		SatGPS_DB::prune( $days );
	}
}
add_action( 'satgps_daily_prune', 'satgps_run_prune' );

// Boot the plugin.
SatGPS_Plugin::instance();
