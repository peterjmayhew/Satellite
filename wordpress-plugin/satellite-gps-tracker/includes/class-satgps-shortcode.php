<?php
/**
 * [satgps_dashboard] shortcode: renders the dashboard on the front end.
 *
 * @package SatelliteGpsTracker
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Front-end dashboard shortcode.
 */
class SatGPS_Shortcode {

	/**
	 * Render the shortcode.
	 *
	 * @param array $atts Shortcode attributes.
	 * @return string
	 */
	public function render( $atts ) {
		$settings = SatGPS_Plugin::settings();

		// Only expose to visitors if public reads are enabled; admins always.
		if ( empty( $settings['allow_public'] ) && ! current_user_can( 'manage_options' ) ) {
			return '<div class="satgps-wrap"><p>' . esc_html__( 'The satellite dashboard is not publicly available.', 'satgps' ) . '</p></div>';
		}

		$atts = shortcode_atts(
			array(
				'device'           => '',
				'height'           => '480',
				'hide_map'         => '',   // "1"/"yes"/"true" hides the map + coordinates.
				'hide_map_message' => '',   // Optional custom text shown in place of the map.
			),
			$atts,
			'satgps_dashboard'
		);

		SatGPS_Admin::enqueue_dashboard_assets( false );

		ob_start();
		echo '<div class="satgps-wrap satgps-frontend" data-device="' . esc_attr( $atts['device'] ) . '" data-map-height="' . esc_attr( (int) $atts['height'] ) . '">';
		include SATGPS_DIR . 'templates/dashboard.php';
		echo '</div>';
		return ob_get_clean();
	}
}
