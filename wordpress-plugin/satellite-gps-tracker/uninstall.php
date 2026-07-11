<?php
/**
 * Runs when the plugin is deleted from wp-admin. Removes the data table,
 * options and scheduled event.
 *
 * @package SatelliteGpsTracker
 */

if ( ! defined( 'WP_UNINSTALL_PLUGIN' ) ) {
	exit;
}

global $wpdb;

$table = $wpdb->prefix . 'satgps_fixes';
// Table name cannot be parameterised; it is a trusted internal identifier.
$wpdb->query( "DROP TABLE IF EXISTS $table" ); // phpcs:ignore WordPress.DB.PreparedSQL

delete_option( 'satgps_settings' );
delete_option( 'satgps_db_version' );

$timestamp = wp_next_scheduled( 'satgps_daily_prune' );
if ( $timestamp ) {
	wp_unschedule_event( $timestamp, 'satgps_daily_prune' );
}
