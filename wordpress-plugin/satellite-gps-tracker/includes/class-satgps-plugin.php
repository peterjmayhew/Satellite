<?php
/**
 * Plugin bootstrap: wires the sub-modules to WordPress hooks.
 *
 * @package SatelliteGpsTracker
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Main coordinator (singleton).
 */
class SatGPS_Plugin {

	/**
	 * Singleton instance.
	 *
	 * @var SatGPS_Plugin|null
	 */
	private static $instance = null;

	/**
	 * REST controller.
	 *
	 * @var SatGPS_REST
	 */
	public $rest;

	/**
	 * Admin controller.
	 *
	 * @var SatGPS_Admin
	 */
	public $admin;

	/**
	 * Shortcode controller.
	 *
	 * @var SatGPS_Shortcode
	 */
	public $shortcode;

	/**
	 * Get / create the singleton.
	 *
	 * @return SatGPS_Plugin
	 */
	public static function instance() {
		if ( null === self::$instance ) {
			self::$instance = new self();
		}
		return self::$instance;
	}

	/**
	 * Constructor: instantiate modules and register hooks.
	 */
	private function __construct() {
		$this->rest      = new SatGPS_REST();
		$this->admin     = new SatGPS_Admin();
		$this->shortcode = new SatGPS_Shortcode();

		add_action( 'rest_api_init', array( $this->rest, 'register_routes' ) );

		if ( is_admin() ) {
			add_action( 'admin_menu', array( $this->admin, 'register_menu' ) );
			add_action( 'admin_init', array( $this->admin, 'register_settings' ) );
			add_action( 'admin_enqueue_scripts', array( $this->admin, 'enqueue_assets' ) );
		}

		add_shortcode( 'satgps_dashboard', array( $this->shortcode, 'render' ) );

		add_action( 'plugins_loaded', array( $this, 'maybe_upgrade_db' ) );
	}

	/**
	 * Run dbDelta if the stored schema version is behind.
	 */
	public function maybe_upgrade_db() {
		if ( get_option( 'satgps_db_version' ) !== SATGPS_DB_VERSION ) {
			SatGPS_DB::create_table();
			update_option( 'satgps_db_version', SATGPS_DB_VERSION );
		}
	}

	/**
	 * Shared accessor for the current settings, merged over defaults.
	 *
	 * @return array
	 */
	public static function settings() {
		$defaults = satgps_default_settings();
		$saved    = get_option( 'satgps_settings', array() );
		if ( ! is_array( $saved ) ) {
			$saved = array();
		}
		return array_merge( $defaults, $saved );
	}
}
