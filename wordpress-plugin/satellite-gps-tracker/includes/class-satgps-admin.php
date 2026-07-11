<?php
/**
 * Admin UI: menu, settings screen and asset loading.
 *
 * @package SatelliteGpsTracker
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Handles the wp-admin side of the plugin.
 */
class SatGPS_Admin {

	/**
	 * Page hook suffixes we own (used to scope asset loading).
	 *
	 * @var string[]
	 */
	private $hooks = array();

	/**
	 * Register the admin menu + sub-pages.
	 */
	public function register_menu() {
		$top = add_menu_page(
			__( 'Satellite GPS', 'satgps' ),
			__( 'Satellite GPS', 'satgps' ),
			'manage_options',
			'satgps-dashboard',
			array( $this, 'render_dashboard' ),
			'dashicons-location-alt',
			58
		);

		$sub = add_submenu_page(
			'satgps-dashboard',
			__( 'Dashboard', 'satgps' ),
			__( 'Dashboard', 'satgps' ),
			'manage_options',
			'satgps-dashboard',
			array( $this, 'render_dashboard' )
		);

		$settings = add_submenu_page(
			'satgps-dashboard',
			__( 'Settings', 'satgps' ),
			__( 'Settings', 'satgps' ),
			'manage_options',
			'satgps-settings',
			array( $this, 'render_settings' )
		);

		$this->hooks = array_filter( array( $top, $sub, $settings ) );
	}

	/**
	 * Register the settings option + sanitiser.
	 */
	public function register_settings() {
		register_setting(
			'satgps_group',
			'satgps_settings',
			array(
				'type'              => 'array',
				'sanitize_callback' => array( $this, 'sanitize_settings' ),
			)
		);
	}

	/**
	 * Sanitise the settings array.
	 *
	 * @param array $input Raw posted values.
	 * @return array
	 */
	public function sanitize_settings( $input ) {
		$out = SatGPS_Plugin::settings();

		$out['allow_public']   = empty( $input['allow_public'] ) ? 0 : 1;
		$out['retention_days'] = isset( $input['retention_days'] ) ? max( 0, (int) $input['retention_days'] ) : 90;
		$out['units']          = ( isset( $input['units'] ) && 'mph' === $input['units'] ) ? 'mph' : 'kmh';
		$out['map_zoom']       = isset( $input['map_zoom'] ) ? min( 19, max( 1, (int) $input['map_zoom'] ) ) : 13;

		if ( empty( $out['api_key'] ) || ! empty( $input['regenerate_key'] ) ) {
			$out['api_key'] = wp_generate_password( 40, false, false );
			add_settings_error( 'satgps_settings', 'satgps_key', __( 'A new API key was generated. Update it on your device.', 'satgps' ), 'updated' );
		}

		return $out;
	}

	/**
	 * Enqueue dashboard assets on our pages only.
	 *
	 * @param string $hook Current admin page hook.
	 */
	public function enqueue_assets( $hook ) {
		if ( ! in_array( $hook, $this->hooks, true ) ) {
			return;
		}
		// Settings page needs no charting libs.
		if ( false !== strpos( $hook, 'satgps-settings' ) ) {
			return;
		}
		self::enqueue_dashboard_assets( true );
	}

	/**
	 * Shared asset loader (used by admin + the public shortcode).
	 *
	 * @param bool $is_admin Whether the current context is the admin dashboard.
	 */
	public static function enqueue_dashboard_assets( $is_admin ) {
		wp_enqueue_style( 'satgps-leaflet', 'https://cdn.jsdelivr.net/npm/leaflet@1.9.4/dist/leaflet.css', array(), '1.9.4' );
		wp_enqueue_style( 'satgps', SATGPS_URL . 'assets/css/satgps.css', array( 'satgps-leaflet' ), SATGPS_VERSION );

		wp_enqueue_script( 'satgps-leaflet', 'https://cdn.jsdelivr.net/npm/leaflet@1.9.4/dist/leaflet.js', array(), '1.9.4', true );
		wp_enqueue_script( 'satgps-chart', 'https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js', array(), '4.4.1', true );
		wp_enqueue_script( 'satgps-skyplot', SATGPS_URL . 'assets/js/satgps-skyplot.js', array(), SATGPS_VERSION, true );
		wp_enqueue_script( 'satgps-dashboard', SATGPS_URL . 'assets/js/satgps-dashboard.js', array( 'satgps-leaflet', 'satgps-chart', 'satgps-skyplot' ), SATGPS_VERSION, true );

		$settings = SatGPS_Plugin::settings();
		wp_localize_script(
			'satgps-dashboard',
			'satgpsData',
			array(
				'restUrl'  => esc_url_raw( rest_url( SATGPS_REST_NS . '/' ) ),
				'nonce'    => wp_create_nonce( 'wp_rest' ),
				'isAdmin'  => (bool) $is_admin,
				'units'    => $settings['units'],
				'mapZoom'  => (int) $settings['map_zoom'],
			)
		);
	}

	/**
	 * Render the dashboard page.
	 */
	public function render_dashboard() {
		if ( ! current_user_can( 'manage_options' ) ) {
			return;
		}
		echo '<div class="wrap satgps-wrap">';
		echo '<h1>' . esc_html__( 'Satellite GPS Tracker', 'satgps' ) . '</h1>';
		include SATGPS_DIR . 'templates/dashboard.php';
		echo '</div>';
	}

	/**
	 * Render the settings page.
	 */
	public function render_settings() {
		if ( ! current_user_can( 'manage_options' ) ) {
			return;
		}
		$settings = SatGPS_Plugin::settings();
		$endpoint = esc_url( rest_url( SATGPS_REST_NS . '/ingest' ) );
		?>
		<div class="wrap">
			<h1><?php esc_html_e( 'Satellite GPS - Settings', 'satgps' ); ?></h1>
			<?php settings_errors( 'satgps_settings' ); ?>

			<h2><?php esc_html_e( 'Device configuration', 'satgps' ); ?></h2>
			<p><?php esc_html_e( 'Copy these two values into your ESP32 secrets.h file:', 'satgps' ); ?></p>
			<table class="form-table" role="presentation">
				<tr>
					<th scope="row"><?php esc_html_e( 'Ingest endpoint', 'satgps' ); ?></th>
					<td><code><?php echo esc_html( $endpoint ); ?></code></td>
				</tr>
				<tr>
					<th scope="row"><?php esc_html_e( 'API key', 'satgps' ); ?></th>
					<td>
						<code class="satgps-key"><?php echo esc_html( $settings['api_key'] ); ?></code>
						<p class="description"><?php esc_html_e( 'Sent by the device in the X-API-Key header. Keep it secret.', 'satgps' ); ?></p>
					</td>
				</tr>
			</table>

			<form method="post" action="options.php">
				<?php settings_fields( 'satgps_group' ); ?>
				<table class="form-table" role="presentation">
					<tr>
						<th scope="row"><?php esc_html_e( 'Public dashboard', 'satgps' ); ?></th>
						<td>
							<label>
								<input type="checkbox" name="satgps_settings[allow_public]" value="1" <?php checked( ! empty( $settings['allow_public'] ) ); ?> />
								<?php esc_html_e( 'Allow non-logged-in visitors to view data (needed for the [satgps_dashboard] shortcode on public pages).', 'satgps' ); ?>
							</label>
						</td>
					</tr>
					<tr>
						<th scope="row"><?php esc_html_e( 'Units', 'satgps' ); ?></th>
						<td>
							<select name="satgps_settings[units]">
								<option value="kmh" <?php selected( $settings['units'], 'kmh' ); ?>><?php esc_html_e( 'km/h', 'satgps' ); ?></option>
								<option value="mph" <?php selected( $settings['units'], 'mph' ); ?>><?php esc_html_e( 'mph', 'satgps' ); ?></option>
							</select>
						</td>
					</tr>
					<tr>
						<th scope="row"><?php esc_html_e( 'Default map zoom', 'satgps' ); ?></th>
						<td><input type="number" min="1" max="19" name="satgps_settings[map_zoom]" value="<?php echo esc_attr( $settings['map_zoom'] ); ?>" /></td>
					</tr>
					<tr>
						<th scope="row"><?php esc_html_e( 'Data retention (days)', 'satgps' ); ?></th>
						<td>
							<input type="number" min="0" name="satgps_settings[retention_days]" value="<?php echo esc_attr( $settings['retention_days'] ); ?>" />
							<p class="description"><?php esc_html_e( '0 = keep forever. Older fixes are pruned daily.', 'satgps' ); ?></p>
						</td>
					</tr>
					<tr>
						<th scope="row"><?php esc_html_e( 'Regenerate API key', 'satgps' ); ?></th>
						<td>
							<label>
								<input type="checkbox" name="satgps_settings[regenerate_key]" value="1" />
								<?php esc_html_e( 'Generate a new key when saving (invalidates the current one).', 'satgps' ); ?>
							</label>
						</td>
					</tr>
				</table>
				<?php submit_button(); ?>
			</form>
		</div>
		<?php
	}
}
