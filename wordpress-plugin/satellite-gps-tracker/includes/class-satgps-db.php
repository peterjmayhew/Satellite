<?php
/**
 * Database layer: schema + all reads/writes for GPS fixes.
 *
 * @package SatelliteGpsTracker
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Data access object for the satgps_fixes table.
 */
class SatGPS_DB {

	/**
	 * Fully-qualified table name.
	 *
	 * @return string
	 */
	public static function table() {
		global $wpdb;
		return $wpdb->prefix . 'satgps_fixes';
	}

	/**
	 * Create / upgrade the table via dbDelta.
	 */
	public static function create_table() {
		global $wpdb;
		require_once ABSPATH . 'wp-admin/includes/upgrade.php';

		$table           = self::table();
		$charset_collate = $wpdb->get_charset_collate();

		// dbDelta is picky about formatting: two spaces after PRIMARY KEY, lower-case types.
		$sql = "CREATE TABLE $table (
			id bigint(20) unsigned NOT NULL AUTO_INCREMENT,
			device varchar(32) NOT NULL DEFAULT '',
			ts datetime NOT NULL DEFAULT '1970-01-01 00:00:00',
			received_at datetime NOT NULL DEFAULT '1970-01-01 00:00:00',
			fix tinyint(1) NOT NULL DEFAULT 0,
			sats_used smallint(5) NOT NULL DEFAULT 0,
			sats_in_view smallint(5) NOT NULL DEFAULT 0,
			lat double NOT NULL DEFAULT 0,
			lon double NOT NULL DEFAULT 0,
			alt_m float NOT NULL DEFAULT 0,
			speed_kmh float NOT NULL DEFAULT 0,
			heading_deg float NOT NULL DEFAULT 0,
			hdop float NOT NULL DEFAULT 0,
			fix_type tinyint(2) NOT NULL DEFAULT 0,
			acc_h_m float NOT NULL DEFAULT 0,
			acc_v_m float NOT NULL DEFAULT 0,
			vspeed_ms float NOT NULL DEFAULT 0,
			hae_m float NOT NULL DEFAULT 0,
			geoid_m float NOT NULL DEFAULT 0,
			vdop float NOT NULL DEFAULT 0,
			pdop float NOT NULL DEFAULT 0,
			speed_acc_ms float NOT NULL DEFAULT 0,
			head_acc_deg float NOT NULL DEFAULT 0,
			sbas tinyint(1) NOT NULL DEFAULT 0,
			jam_state tinyint(1) NOT NULL DEFAULT 0,
			jam_ind smallint(5) NOT NULL DEFAULT 0,
			agc_pct tinyint(3) NOT NULL DEFAULT 0,
			ant_status tinyint(1) NOT NULL DEFAULT 0,
			spoof_state tinyint(1) NOT NULL DEFAULT 0,
			ttff_ms int(10) unsigned NOT NULL DEFAULT 0,
			noise smallint(5) unsigned NOT NULL DEFAULT 0,
			odo_m int(10) unsigned NOT NULL DEFAULT 0,
			odo_total_m int(10) unsigned NOT NULL DEFAULT 0,
			err_major_m float NOT NULL DEFAULT 0,
			err_minor_m float NOT NULL DEFAULT 0,
			err_orient_deg float NOT NULL DEFAULT 0,
			uptime_ms bigint(20) unsigned NOT NULL DEFAULT 0,
			sats_json longtext NULL,
			PRIMARY KEY  (id),
			KEY device_ts (device, ts),
			KEY received_at (received_at)
		) $charset_collate;";

		dbDelta( $sql );
	}

	/**
	 * Insert one fix. Expects already-sanitised values.
	 *
	 * @param array $d Sanitised fix data.
	 * @return int|false Inserted row id or false on failure.
	 */
	public static function insert_fix( array $d ) {
		global $wpdb;

		$ok = $wpdb->insert(
			self::table(),
			array(
				'device'       => $d['device'],
				'ts'           => $d['ts'],
				'received_at'  => $d['received_at'],
				'fix'          => $d['fix'],
				'sats_used'    => $d['sats_used'],
				'sats_in_view' => $d['sats_in_view'],
				'lat'          => $d['lat'],
				'lon'          => $d['lon'],
				'alt_m'        => $d['alt_m'],
				'speed_kmh'    => $d['speed_kmh'],
				'heading_deg'  => $d['heading_deg'],
				'hdop'         => $d['hdop'],
				'fix_type'     => $d['fix_type'],
				'acc_h_m'      => $d['acc_h_m'],
				'acc_v_m'      => $d['acc_v_m'],
				'vspeed_ms'    => $d['vspeed_ms'],
				'hae_m'        => $d['hae_m'],
				'geoid_m'      => $d['geoid_m'],
				'vdop'         => $d['vdop'],
				'pdop'         => $d['pdop'],
				'speed_acc_ms' => $d['speed_acc_ms'],
				'head_acc_deg' => $d['head_acc_deg'],
				'sbas'         => $d['sbas'],
				'jam_state'    => $d['jam_state'],
				'jam_ind'      => $d['jam_ind'],
				'agc_pct'      => $d['agc_pct'],
				'ant_status'   => $d['ant_status'],
				'spoof_state'  => $d['spoof_state'],
				'ttff_ms'      => $d['ttff_ms'],
				'noise'        => $d['noise'],
				'odo_m'        => $d['odo_m'],
				'odo_total_m'  => $d['odo_total_m'],
				'err_major_m'  => $d['err_major_m'],
				'err_minor_m'  => $d['err_minor_m'],
				'err_orient_deg' => $d['err_orient_deg'],
				'uptime_ms'    => $d['uptime_ms'],
				'sats_json'    => $d['sats_json'],
			),
			array(
				'%s', '%s', '%s', '%d', '%d', '%d', '%f', '%f', '%f', '%f', '%f', '%f',
				'%d', '%f', '%f', '%f', '%f', '%f', '%f', '%f', '%f', '%f', '%d',
				'%d', '%d', '%d', '%d', '%d', '%d',
				'%d', '%d', '%d',
				'%f', '%f', '%f',
				'%d', '%s',
			)
		);

		return $ok ? (int) $wpdb->insert_id : false;
	}

	/**
	 * The most recent fix (optionally for a specific device).
	 *
	 * @param string $device Device id, or '' for any.
	 * @return array|null Row as associative array, or null.
	 */
	public static function get_latest( $device = '' ) {
		global $wpdb;
		$table = self::table();

		if ( '' !== $device ) {
			$row = $wpdb->get_row(
				$wpdb->prepare( "SELECT * FROM $table WHERE device = %s ORDER BY ts DESC, id DESC LIMIT 1", $device ),
				ARRAY_A
			);
		} else {
			$row = $wpdb->get_row( "SELECT * FROM $table ORDER BY ts DESC, id DESC LIMIT 1", ARRAY_A );
		}
		return $row ? $row : null;
	}

	/**
	 * A track (ordered list of fixes) for a device within a time window.
	 *
	 * @param string $device Device id.
	 * @param string $from   MySQL datetime lower bound (UTC) or ''.
	 * @param string $to     MySQL datetime upper bound (UTC) or ''.
	 * @param int    $limit  Max rows (capped).
	 * @return array List of rows.
	 */
	public static function get_track( $device, $from = '', $to = '', $limit = 2000 ) {
		global $wpdb;
		$table = self::table();

		$limit = max( 1, min( 10000, (int) $limit ) );
		$where = 'fix = 1';
		$args  = array();

		if ( '' !== $device ) {
			$where .= ' AND device = %s';
			$args[] = $device;
		}
		if ( '' !== $from ) {
			$where .= ' AND ts >= %s';
			$args[] = $from;
		}
		if ( '' !== $to ) {
			$where .= ' AND ts <= %s';
			$args[] = $to;
		}

		$sql = "SELECT id, device, ts, lat, lon, alt_m, speed_kmh, heading_deg, hdop,
					acc_h_m, vspeed_ms, sats_used
				FROM $table WHERE $where ORDER BY ts ASC, id ASC LIMIT %d";
		$args[] = $limit;

		return $wpdb->get_results( $wpdb->prepare( $sql, $args ), ARRAY_A );
	}

	/**
	 * Distinct devices with last-seen time and row counts.
	 *
	 * @return array
	 */
	public static function get_devices() {
		global $wpdb;
		$table = self::table();
		return $wpdb->get_results(
			"SELECT device, COUNT(*) AS points, MAX(ts) AS last_ts, MAX(received_at) AS last_seen
			 FROM $table GROUP BY device ORDER BY last_seen DESC",
			ARRAY_A
		);
	}

	/**
	 * Aggregate statistics for a device / window.
	 *
	 * @param string $device Device id.
	 * @param string $from   MySQL datetime or ''.
	 * @param string $to     MySQL datetime or ''.
	 * @return array
	 */
	public static function get_stats( $device, $from = '', $to = '' ) {
		global $wpdb;
		$table = self::table();

		$where = 'fix = 1';
		$args  = array();
		if ( '' !== $device ) {
			$where .= ' AND device = %s';
			$args[] = $device;
		}
		if ( '' !== $from ) {
			$where .= ' AND ts >= %s';
			$args[] = $from;
		}
		if ( '' !== $to ) {
			$where .= ' AND ts <= %s';
			$args[] = $to;
		}

		$sql = "SELECT COUNT(*) AS points, MAX(speed_kmh) AS max_speed, AVG(speed_kmh) AS avg_speed,
					MAX(alt_m) AS max_alt, MIN(alt_m) AS min_alt, MAX(sats_used) AS max_sats,
					MIN(ts) AS first_ts, MAX(ts) AS last_ts
				FROM $table WHERE $where";

		$row = $args
			? $wpdb->get_row( $wpdb->prepare( $sql, $args ), ARRAY_A )
			: $wpdb->get_row( $sql, ARRAY_A );

		return $row ? $row : array();
	}

	/**
	 * Delete fixes older than N days (by received_at).
	 *
	 * @param int $days Age threshold in days.
	 * @return int Rows deleted.
	 */
	public static function prune( $days ) {
		global $wpdb;
		$table = self::table();
		$days  = max( 1, (int) $days );
		return (int) $wpdb->query(
			$wpdb->prepare( "DELETE FROM $table WHERE received_at < ( UTC_TIMESTAMP() - INTERVAL %d DAY )", $days )
		);
	}
}
