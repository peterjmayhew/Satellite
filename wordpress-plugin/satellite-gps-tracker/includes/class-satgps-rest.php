<?php
/**
 * REST API: telemetry ingest (from the ESP32) + read endpoints (for dashboards).
 *
 * @package SatelliteGpsTracker
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Registers and handles all REST routes under satgps/v1.
 */
class SatGPS_REST {

	/**
	 * Register routes.
	 */
	public function register_routes() {
		register_rest_route(
			SATGPS_REST_NS,
			'/ingest',
			array(
				'methods'             => 'POST',
				'callback'            => array( $this, 'ingest' ),
				'permission_callback' => array( $this, 'can_ingest' ),
			)
		);

		$read_perm = array( $this, 'can_read' );

		register_rest_route(
			SATGPS_REST_NS,
			'/latest',
			array(
				'methods'             => 'GET',
				'callback'            => array( $this, 'latest' ),
				'permission_callback' => $read_perm,
				'args'                => array( 'device' => array( 'sanitize_callback' => 'sanitize_text_field' ) ),
			)
		);

		register_rest_route(
			SATGPS_REST_NS,
			'/track',
			array(
				'methods'             => 'GET',
				'callback'            => array( $this, 'track' ),
				'permission_callback' => $read_perm,
			)
		);

		register_rest_route(
			SATGPS_REST_NS,
			'/stats',
			array(
				'methods'             => 'GET',
				'callback'            => array( $this, 'stats' ),
				'permission_callback' => $read_perm,
			)
		);

		register_rest_route(
			SATGPS_REST_NS,
			'/devices',
			array(
				'methods'             => 'GET',
				'callback'            => array( $this, 'devices' ),
				'permission_callback' => $read_perm,
			)
		);
	}

	/* ------------------------------------------------------------------ */
	/*  Permissions                                                        */
	/* ------------------------------------------------------------------ */

	/**
	 * Ingest auth: constant-time comparison of the X-API-Key header.
	 *
	 * @param WP_REST_Request $request Request.
	 * @return true|WP_Error
	 */
	public function can_ingest( $request ) {
		$settings = SatGPS_Plugin::settings();
		$expected = isset( $settings['api_key'] ) ? (string) $settings['api_key'] : '';

		if ( '' === $expected ) {
			return new WP_Error( 'satgps_no_key', 'API key is not configured on the server.', array( 'status' => 503 ) );
		}

		$provided = (string) $request->get_header( 'X-API-Key' );
		if ( '' === $provided || ! hash_equals( $expected, $provided ) ) {
			return new WP_Error( 'satgps_unauthorized', 'Invalid or missing API key.', array( 'status' => 401 ) );
		}
		return true;
	}

	/**
	 * Read auth: admins always; everyone else only if public reads are enabled.
	 *
	 * @return bool
	 */
	public function can_read() {
		$settings = SatGPS_Plugin::settings();
		if ( ! empty( $settings['allow_public'] ) ) {
			return true;
		}
		return current_user_can( 'manage_options' );
	}

	/* ------------------------------------------------------------------ */
	/*  Ingest                                                             */
	/* ------------------------------------------------------------------ */

	/**
	 * Store one telemetry packet.
	 *
	 * @param WP_REST_Request $request Request.
	 * @return WP_REST_Response|WP_Error
	 */
	public function ingest( $request ) {
		$body = $request->get_json_params();
		if ( ! is_array( $body ) ) {
			return new WP_Error( 'satgps_bad_body', 'Expected a JSON object.', array( 'status' => 400 ) );
		}

		$device = ( isset( $body['device'] ) && is_scalar( $body['device'] ) ) ? sanitize_text_field( (string) $body['device'] ) : 'unknown';
		$device = '' === $device ? 'unknown' : substr( $device, 0, 32 );

		// Timestamp: prefer the device's ISO ts; fall back to server receive time.
		$received_at = current_time( 'mysql', true ); // GMT.
		$ts          = $received_at;
		if ( ! empty( $body['ts'] ) && is_scalar( $body['ts'] ) ) {
			$parsed = strtotime( (string) $body['ts'] );
			if ( false !== $parsed ) {
				$ts = gmdate( 'Y-m-d H:i:s', $parsed );
			}
		}

		$lat = isset( $body['lat'] ) ? (float) $body['lat'] : 0.0;
		$lon = isset( $body['lon'] ) ? (float) $body['lon'] : 0.0;
		if ( $lat < -90 || $lat > 90 ) {
			$lat = 0.0;
		}
		if ( $lon < -180 || $lon > 180 ) {
			$lon = 0.0;
		}

		$data = array(
			'device'       => $device,
			'ts'           => $ts,
			'received_at'  => $received_at,
			'fix'          => ! empty( $body['fix'] ) ? 1 : 0,
			'sats_used'    => isset( $body['sats_used'] ) ? max( 0, (int) $body['sats_used'] ) : 0,
			'sats_in_view' => isset( $body['sats_in_view'] ) ? max( 0, (int) $body['sats_in_view'] ) : 0,
			'lat'          => $lat,
			'lon'          => $lon,
			'alt_m'        => isset( $body['alt_m'] ) ? (float) $body['alt_m'] : 0.0,
			'speed_kmh'    => isset( $body['speed_kmh'] ) ? max( 0.0, (float) $body['speed_kmh'] ) : 0.0,
			'heading_deg'  => isset( $body['heading_deg'] ) ? (float) $body['heading_deg'] : 0.0,
			'hdop'         => isset( $body['hdop'] ) ? max( 0.0, (float) $body['hdop'] ) : 0.0,
			'fix_type'     => isset( $body['fix_type'] ) ? max( 0, (int) $body['fix_type'] ) : 0,
			'acc_h_m'      => isset( $body['acc_h_m'] ) ? max( 0.0, (float) $body['acc_h_m'] ) : 0.0,
			'acc_v_m'      => isset( $body['acc_v_m'] ) ? max( 0.0, (float) $body['acc_v_m'] ) : 0.0,
			'vspeed_ms'    => isset( $body['vspeed_ms'] ) ? (float) $body['vspeed_ms'] : 0.0,
			'hae_m'        => isset( $body['hae_m'] ) ? (float) $body['hae_m'] : 0.0,
			'geoid_m'      => isset( $body['geoid_m'] ) ? (float) $body['geoid_m'] : 0.0,
			'vdop'         => isset( $body['vdop'] ) ? max( 0.0, (float) $body['vdop'] ) : 0.0,
			'pdop'         => isset( $body['pdop'] ) ? max( 0.0, (float) $body['pdop'] ) : 0.0,
			'speed_acc_ms' => isset( $body['speed_acc_ms'] ) ? max( 0.0, (float) $body['speed_acc_ms'] ) : 0.0,
			'head_acc_deg' => isset( $body['head_acc_deg'] ) ? max( 0.0, (float) $body['head_acc_deg'] ) : 0.0,
			'sbas'         => ! empty( $body['sbas'] ) ? 1 : 0,
			'jam_state'    => isset( $body['jam_state'] ) ? max( 0, min( 3, (int) $body['jam_state'] ) ) : 0,
			'jam_ind'      => isset( $body['jam_ind'] ) ? max( 0, min( 255, (int) $body['jam_ind'] ) ) : 0,
			'agc_pct'      => isset( $body['agc_pct'] ) ? max( 0, min( 100, (int) $body['agc_pct'] ) ) : 0,
			'ant_status'   => isset( $body['ant_status'] ) ? max( 0, min( 4, (int) $body['ant_status'] ) ) : 0,
			'spoof_state'  => isset( $body['spoof_state'] ) ? max( 0, min( 3, (int) $body['spoof_state'] ) ) : 0,
			'ttff_ms'      => isset( $body['ttff_ms'] ) ? max( 0, (int) $body['ttff_ms'] ) : 0,
			'noise'        => isset( $body['noise'] ) ? max( 0, min( 65535, (int) $body['noise'] ) ) : 0,
			'odo_m'        => isset( $body['odo_m'] ) ? max( 0, (int) $body['odo_m'] ) : 0,
			'odo_total_m'  => isset( $body['odo_total_m'] ) ? max( 0, (int) $body['odo_total_m'] ) : 0,
			'err_major_m'  => isset( $body['err_major_m'] ) ? max( 0.0, (float) $body['err_major_m'] ) : 0.0,
			'err_minor_m'  => isset( $body['err_minor_m'] ) ? max( 0.0, (float) $body['err_minor_m'] ) : 0.0,
			'err_orient_deg' => isset( $body['err_orient_deg'] ) ? (float) $body['err_orient_deg'] : 0.0,
			'sbas_sys'     => isset( $body['sbas_sys'] ) ? max( -1, min( 16, (int) $body['sbas_sys'] ) ) : -1,
			'sbas_prn'     => isset( $body['sbas_prn'] ) ? max( 0, min( 210, (int) $body['sbas_prn'] ) ) : 0,
			'sbas_cnt'     => isset( $body['sbas_cnt'] ) ? max( 0, min( 64, (int) $body['sbas_cnt'] ) ) : 0,
			'rx_module'    => isset( $body['rx_module'] ) && is_scalar( $body['rx_module'] ) ? substr( sanitize_text_field( (string) $body['rx_module'] ), 0, 24 ) : '',
			'rx_fw'        => isset( $body['rx_fw'] ) && is_scalar( $body['rx_fw'] ) ? substr( sanitize_text_field( (string) $body['rx_fw'] ), 0, 32 ) : '',
			'rx_proto'     => isset( $body['rx_proto'] ) && is_scalar( $body['rx_proto'] ) ? substr( sanitize_text_field( (string) $body['rx_proto'] ), 0, 12 ) : '',
			'rx_gnss'      => isset( $body['rx_gnss'] ) && is_scalar( $body['rx_gnss'] ) ? substr( sanitize_text_field( (string) $body['rx_gnss'] ), 0, 48 ) : '',
			'uart_tx_pct'  => isset( $body['uart_tx_pct'] ) ? max( -1, min( 100, (int) $body['uart_tx_pct'] ) ) : -1,
			'uart_tx_peak' => isset( $body['uart_tx_peak'] ) ? max( -1, min( 100, (int) $body['uart_tx_peak'] ) ) : -1,
			'uart_rx_pct'  => isset( $body['uart_rx_pct'] ) ? max( -1, min( 100, (int) $body['uart_rx_pct'] ) ) : -1,
			'uart_ovf'     => isset( $body['uart_ovf'] ) ? max( 0, min( 65535, (int) $body['uart_ovf'] ) ) : 0,
			'geo_state'    => isset( $body['geo_state'] ) ? max( -1, min( 2, (int) $body['geo_state'] ) ) : -1,
			'geo_status'   => ! empty( $body['geo_status'] ) ? 1 : 0,
			'geo_lat'      => isset( $body['geo_lat'] ) ? max( -90.0, min( 90.0, (float) $body['geo_lat'] ) ) : 0.0,
			'geo_lon'      => isset( $body['geo_lon'] ) ? max( -180.0, min( 180.0, (float) $body['geo_lon'] ) ) : 0.0,
			'geo_radius_m' => isset( $body['geo_radius_m'] ) ? max( 0, min( 1000000, (int) $body['geo_radius_m'] ) ) : 0,
			'uptime_ms'    => isset( $body['uptime_ms'] ) ? max( 0, (int) $body['uptime_ms'] ) : 0,
			'sats_json'    => wp_json_encode( $this->sanitize_sats( isset( $body['sats'] ) ? $body['sats'] : array() ) ),
		);

		$id = SatGPS_DB::insert_fix( $data );
		if ( ! $id ) {
			return new WP_Error( 'satgps_store_failed', 'Could not store telemetry.', array( 'status' => 500 ) );
		}

		// Live RF spectrum (UBX-MON-SPAN) is bulky and live-only, so it is NOT stored
		// per-row; keep just the latest snapshot per device in an option.
		$this->store_spectrum( $device, $body );

		return rest_ensure_response(
			array(
				'ok'          => true,
				'id'          => $id,
				'received_at' => $received_at,
			)
		);
	}

	/**
	 * Clean and cap the satellite array.
	 *
	 * @param mixed $sats Raw sats value from the request.
	 * @return array Sanitised list.
	 */
	private function sanitize_sats( $sats ) {
		if ( ! is_array( $sats ) ) {
			return array();
		}
		$out = array();
		foreach ( $sats as $s ) {
			if ( ! is_array( $s ) ) {
				continue;
			}
			$c = isset( $s['c'] ) ? preg_replace( '/[^A-Za-z]/', '', (string) $s['c'] ) : '?';
			$out[] = array(
				'c'   => '' === $c ? '?' : strtoupper( substr( $c, 0, 1 ) ),
				'prn' => isset( $s['prn'] ) ? (int) $s['prn'] : 0,
				'el'  => isset( $s['el'] ) ? (int) $s['el'] : 0,
				'az'  => isset( $s['az'] ) ? (int) $s['az'] : 0,
				'snr' => isset( $s['snr'] ) ? (int) $s['snr'] : -1,
				'u'   => ! empty( $s['u'] ) ? 1 : 0,
			);
			if ( count( $out ) >= 64 ) {
				break;
			}
		}
		return $out;
	}

	/**
	 * Option name holding the latest RF spectrum for a device.
	 *
	 * @param string $device Sanitised device id.
	 * @return string
	 */
	private function spectrum_option( $device ) {
		return 'satgps_spectrum_' . $device;
	}

	/**
	 * Store the latest RF-spectrum snapshot (UBX-MON-SPAN) for a device, overwriting
	 * the previous one. Kept out of the fixes table because it is bulky and only the
	 * live view matters. Ignored if the packet carries no valid spectrum.
	 *
	 * @param string $device Sanitised device id.
	 * @param array  $body   Raw request body.
	 */
	private function store_spectrum( $device, $body ) {
		if ( empty( $body['spectrum'] ) || ! is_scalar( $body['spectrum'] ) ) {
			return;
		}
		$hex = strtolower( preg_replace( '/[^0-9a-fA-F]/', '', (string) $body['spectrum'] ) );
		$hex = substr( $hex, 0, 1024 );          // <= 512 bins * 2 hex chars
		if ( strlen( $hex ) < 2 ) {
			return;
		}
		update_option(
			$this->spectrum_option( $device ),
			array(
				'hex'       => $hex,
				'center_hz' => isset( $body['spec_center_hz'] ) ? max( 0, (int) $body['spec_center_hz'] ) : 0,
				'span_hz'   => isset( $body['spec_span_hz'] ) ? max( 0, (int) $body['spec_span_hz'] ) : 0,
				'res_hz'    => isset( $body['spec_res_hz'] ) ? max( 0, (int) $body['spec_res_hz'] ) : 0,
				'pga'       => isset( $body['spec_pga'] ) ? max( 0, min( 255, (int) $body['spec_pga'] ) ) : 0,
				't'         => time(),
			),
			false // do not autoload.
		);
	}

	/* ------------------------------------------------------------------ */
	/*  Reads                                                              */
	/* ------------------------------------------------------------------ */

	/**
	 * Latest fix + decoded sky snapshot.
	 *
	 * @param WP_REST_Request $request Request.
	 * @return WP_REST_Response
	 */
	public function latest( $request ) {
		$device_raw = $request->get_param( 'device' );
		$device     = is_scalar( $device_raw ) ? sanitize_text_field( (string) $device_raw ) : '';
		$row    = SatGPS_DB::get_latest( $device );

		if ( ! $row ) {
			return rest_ensure_response( array( 'found' => false ) );
		}

		$sats = json_decode( isset( $row['sats_json'] ) ? $row['sats_json'] : '[]', true );
		if ( ! is_array( $sats ) ) {
			$sats = array();
		}

		$age = max( 0, time() - (int) strtotime( $row['received_at'] . ' UTC' ) );

		$out = array(
				'found'        => true,
				'device'       => $row['device'],
				'ts'           => $row['ts'],
				'received_at'  => $row['received_at'],
				'age_seconds'  => $age,
				'fix'          => (int) $row['fix'],
				'fix_type'     => (int) $row['fix_type'],
				'lat'          => (float) $row['lat'],
				'lon'          => (float) $row['lon'],
				'alt_m'        => (float) $row['alt_m'],
				'hae_m'        => (float) $row['hae_m'],
				'geoid_m'      => (float) $row['geoid_m'],
				'speed_kmh'    => (float) $row['speed_kmh'],
				'vspeed_ms'    => (float) $row['vspeed_ms'],
				'heading_deg'  => (float) $row['heading_deg'],
				'hdop'         => (float) $row['hdop'],
				'vdop'         => (float) $row['vdop'],
				'pdop'         => (float) $row['pdop'],
				'acc_h_m'      => (float) $row['acc_h_m'],
				'acc_v_m'      => (float) $row['acc_v_m'],
				'speed_acc_ms' => (float) $row['speed_acc_ms'],
				'head_acc_deg' => (float) $row['head_acc_deg'],
				'sbas'         => (int) $row['sbas'],
				'jam_state'    => (int) $row['jam_state'],
				'jam_ind'      => (int) $row['jam_ind'],
				'agc_pct'      => (int) $row['agc_pct'],
				'ant_status'   => (int) $row['ant_status'],
				'spoof_state'  => (int) $row['spoof_state'],
				'ttff_ms'      => (int) $row['ttff_ms'],
				'noise'        => isset( $row['noise'] ) ? (int) $row['noise'] : null,
				'odo_m'        => isset( $row['odo_m'] ) ? (int) $row['odo_m'] : null,
				'odo_total_m'  => isset( $row['odo_total_m'] ) ? (int) $row['odo_total_m'] : null,
				'err_major_m'  => (float) $row['err_major_m'],
				'err_minor_m'  => (float) $row['err_minor_m'],
				'err_orient_deg' => (float) $row['err_orient_deg'],
				'sbas_sys'     => isset( $row['sbas_sys'] ) ? (int) $row['sbas_sys'] : -1,
				'sbas_prn'     => isset( $row['sbas_prn'] ) ? (int) $row['sbas_prn'] : 0,
				'sbas_cnt'     => isset( $row['sbas_cnt'] ) ? (int) $row['sbas_cnt'] : 0,
				'rx_module'    => isset( $row['rx_module'] ) ? (string) $row['rx_module'] : '',
				'rx_fw'        => isset( $row['rx_fw'] ) ? (string) $row['rx_fw'] : '',
				'rx_proto'     => isset( $row['rx_proto'] ) ? (string) $row['rx_proto'] : '',
				'rx_gnss'      => isset( $row['rx_gnss'] ) ? (string) $row['rx_gnss'] : '',
				'uart_tx_pct'  => isset( $row['uart_tx_pct'] ) ? (int) $row['uart_tx_pct'] : -1,
				'uart_tx_peak' => isset( $row['uart_tx_peak'] ) ? (int) $row['uart_tx_peak'] : -1,
				'uart_rx_pct'  => isset( $row['uart_rx_pct'] ) ? (int) $row['uart_rx_pct'] : -1,
				'uart_ovf'     => isset( $row['uart_ovf'] ) ? (int) $row['uart_ovf'] : 0,
				'geo_state'    => isset( $row['geo_state'] ) ? (int) $row['geo_state'] : -1,
				'geo_status'   => isset( $row['geo_status'] ) ? (int) $row['geo_status'] : 0,
				'geo_lat'      => isset( $row['geo_lat'] ) ? (float) $row['geo_lat'] : 0.0,
				'geo_lon'      => isset( $row['geo_lon'] ) ? (float) $row['geo_lon'] : 0.0,
				'geo_radius_m' => isset( $row['geo_radius_m'] ) ? (int) $row['geo_radius_m'] : 0,
				'sats_used'    => (int) $row['sats_used'],
				'sats_in_view' => (int) $row['sats_in_view'],
				'sats'         => $sats,
			);

		// Merge the latest RF spectrum snapshot (stored out-of-row; see store_spectrum()).
		$spec = get_option( $this->spectrum_option( $row['device'] ) );
		if ( is_array( $spec ) && ! empty( $spec['hex'] ) ) {
			$out['spectrum']       = (string) $spec['hex'];
			$out['spec_center_hz'] = isset( $spec['center_hz'] ) ? (int) $spec['center_hz'] : 0;
			$out['spec_span_hz']   = isset( $spec['span_hz'] ) ? (int) $spec['span_hz'] : 0;
			$out['spec_res_hz']    = isset( $spec['res_hz'] ) ? (int) $spec['res_hz'] : 0;
			$out['spec_pga']       = isset( $spec['pga'] ) ? (int) $spec['pga'] : 0;
			$out['spec_age']       = isset( $spec['t'] ) ? max( 0, time() - (int) $spec['t'] ) : null;
		}

		return rest_ensure_response( $out );
	}

	/**
	 * A track for the map + charts.
	 *
	 * @param WP_REST_Request $request Request.
	 * @return WP_REST_Response
	 */
	public function track( $request ) {
		$device_raw = $request->get_param( 'device' );
		$device     = is_scalar( $device_raw ) ? sanitize_text_field( (string) $device_raw ) : '';
		$from   = $this->to_mysql_utc( $request->get_param( 'from' ) );
		$to     = $this->to_mysql_utc( $request->get_param( 'to' ) );
		$limit  = (int) $request->get_param( 'limit' );
		if ( $limit <= 0 ) {
			$limit = 2000;
		}

		$rows   = SatGPS_DB::get_track( $device, $from, $to, $limit );
		$points = array();
		foreach ( $rows as $r ) {
			$points[] = array(
				'ts'          => $r['ts'],
				'lat'         => (float) $r['lat'],
				'lon'         => (float) $r['lon'],
				'alt_m'       => (float) $r['alt_m'],
				'speed_kmh'   => (float) $r['speed_kmh'],
				'vspeed_ms'   => (float) $r['vspeed_ms'],
				'heading_deg' => (float) $r['heading_deg'],
				'hdop'        => (float) $r['hdop'],
				'acc_h_m'     => (float) $r['acc_h_m'],
				'sats_used'   => (int) $r['sats_used'],
			);
		}

		return rest_ensure_response(
			array(
				'device' => $device,
				'count'  => count( $points ),
				'points' => $points,
			)
		);
	}

	/**
	 * Aggregate stats incl. distance travelled (haversine over the track).
	 *
	 * @param WP_REST_Request $request Request.
	 * @return WP_REST_Response
	 */
	public function stats( $request ) {
		$device_raw = $request->get_param( 'device' );
		$device     = is_scalar( $device_raw ) ? sanitize_text_field( (string) $device_raw ) : '';
		$from   = $this->to_mysql_utc( $request->get_param( 'from' ) );
		$to     = $this->to_mysql_utc( $request->get_param( 'to' ) );

		$agg  = SatGPS_DB::get_stats( $device, $from, $to );
		$rows = SatGPS_DB::get_track( $device, $from, $to, 10000 );

		$distance_km = 0.0;
		$moving_s    = 0.0;
		$prev        = null;
		foreach ( $rows as $r ) {
			if ( null !== $prev ) {
				$distance_km += $this->haversine_km(
					(float) $prev['lat'],
					(float) $prev['lon'],
					(float) $r['lat'],
					(float) $r['lon']
				);
				$dt = strtotime( $r['ts'] ) - strtotime( $prev['ts'] );
				if ( $dt > 0 && $dt < 30 && (float) $r['speed_kmh'] > 0.5 ) {
					$moving_s += $dt;
				}
			}
			$prev = $r;
		}

		return rest_ensure_response(
			array(
				'device'      => $device,
				'points'      => isset( $agg['points'] ) ? (int) $agg['points'] : 0,
				'distance_km' => round( $distance_km, 3 ),
				'moving_s'    => (int) $moving_s,
				'max_speed'   => isset( $agg['max_speed'] ) ? round( (float) $agg['max_speed'], 1 ) : 0,
				'avg_speed'   => isset( $agg['avg_speed'] ) ? round( (float) $agg['avg_speed'], 1 ) : 0,
				'max_alt'     => isset( $agg['max_alt'] ) ? round( (float) $agg['max_alt'], 1 ) : 0,
				'min_alt'     => isset( $agg['min_alt'] ) ? round( (float) $agg['min_alt'], 1 ) : 0,
				'max_sats'    => isset( $agg['max_sats'] ) ? (int) $agg['max_sats'] : 0,
				'first_ts'    => isset( $agg['first_ts'] ) ? $agg['first_ts'] : null,
				'last_ts'     => isset( $agg['last_ts'] ) ? $agg['last_ts'] : null,
			)
		);
	}

	/**
	 * Device list.
	 *
	 * @return WP_REST_Response
	 */
	public function devices() {
		return rest_ensure_response( SatGPS_DB::get_devices() );
	}

	/* ------------------------------------------------------------------ */
	/*  Helpers                                                            */
	/* ------------------------------------------------------------------ */

	/**
	 * Parse a user-supplied date/time to a MySQL UTC string, or '' if empty/invalid.
	 *
	 * @param mixed $value Raw param.
	 * @return string
	 */
	private function to_mysql_utc( $value ) {
		$value = is_string( $value ) ? trim( $value ) : '';
		if ( '' === $value ) {
			return '';
		}
		$t = strtotime( $value );
		return false === $t ? '' : gmdate( 'Y-m-d H:i:s', $t );
	}

	/**
	 * Great-circle distance in km.
	 *
	 * @param float $lat1 Latitude 1.
	 * @param float $lon1 Longitude 1.
	 * @param float $lat2 Latitude 2.
	 * @param float $lon2 Longitude 2.
	 * @return float
	 */
	private function haversine_km( $lat1, $lon1, $lat2, $lon2 ) {
		$r    = 6371.0088;
		$dlat = deg2rad( $lat2 - $lat1 );
		$dlon = deg2rad( $lon2 - $lon1 );
		$slat = sin( $dlat / 2 );
		$slon = sin( $dlon / 2 );
		$a    = $slat * $slat + cos( deg2rad( $lat1 ) ) * cos( deg2rad( $lat2 ) ) * $slon * $slon;
		return $r * 2 * atan2( sqrt( $a ), sqrt( 1 - $a ) );
	}
}
