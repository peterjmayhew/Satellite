/* Satellite GPS Tracker - sky plot renderer (vanilla canvas). */
(function (window) {
	'use strict';

	// Device talker letter -> RINEX-style constellation code + label.
	var CONST = {
		P: { code: 'G', name: 'GPS',     color: '#4c8bf5' },
		L: { code: 'R', name: 'GLONASS', color: '#f5a623' },
		A: { code: 'E', name: 'Galileo', color: '#9b59b6' },
		B: { code: 'C', name: 'BeiDou',  color: '#e24a6b' },
		Q: { code: 'J', name: 'QZSS',    color: '#17b8b8' }
	};

	function snrColor(snr) {
		if (snr === undefined || snr === null || snr < 0) return '#9ca3af';
		if (snr < 20) return '#ef4444';
		if (snr < 35) return '#f59e0b';
		return '#22c55e';
	}

	function constInfo(c) {
		return CONST[c] || { code: '?', name: 'Other', color: '#9ca3af' };
	}

	function draw(canvas, sats) {
		if (!canvas || !canvas.getContext) return;
		var ctx = canvas.getContext('2d');

		// High-DPI crispness.
		var cssW = canvas.clientWidth || 320;
		var cssH = canvas.clientHeight || cssW;
		var size = Math.min(cssW, cssH) || 320;
		var dpr = window.devicePixelRatio || 1;
		canvas.width = size * dpr;
		canvas.height = size * dpr;
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

		var cx = size / 2;
		var cy = size / 2;
		var R = size / 2 - 18;

		ctx.clearRect(0, 0, size, size);

		// Elevation rings (0, 30, 60 deg) + zenith.
		ctx.strokeStyle = 'rgba(150,163,182,0.35)';
		ctx.fillStyle = 'rgba(150,163,182,0.7)';
		ctx.lineWidth = 1;
		ctx.font = '10px sans-serif';
		[0, 30, 60].forEach(function (el) {
			var r = R * (90 - el) / 90;
			ctx.beginPath();
			ctx.arc(cx, cy, r, 0, Math.PI * 2);
			ctx.stroke();
			if (el > 0) {
				ctx.fillText(el + '°', cx + 2, cy - r + 11);
			}
		});

		// Cross-hairs.
		ctx.beginPath();
		ctx.moveTo(cx - R, cy); ctx.lineTo(cx + R, cy);
		ctx.moveTo(cx, cy - R); ctx.lineTo(cx, cy + R);
		ctx.stroke();

		// Cardinal labels.
		ctx.fillStyle = '#97a3b6';
		ctx.font = 'bold 12px sans-serif';
		ctx.textAlign = 'center';
		ctx.textBaseline = 'middle';
		ctx.fillText('N', cx, cy - R - 9);
		ctx.fillText('S', cx, cy + R + 9);
		ctx.fillText('E', cx + R + 9, cy);
		ctx.fillText('W', cx - R - 9, cy);

		// Satellites.
		(sats || []).forEach(function (s) {
			var el = Number(s.el);
			var az = Number(s.az);
			if (isNaN(el) || isNaN(az) || el < 0 || el > 90 || az < 0 || az >= 360) return;

			var r = R * (90 - el) / 90;
			var rad = az * Math.PI / 180;
			var x = cx + r * Math.sin(rad);
			var y = cy - r * Math.cos(rad);

			// Satellites used in the position fix get a bright ring; others dimmed.
			var used = !!s.u;
			ctx.globalAlpha = used ? 1 : 0.5;
			ctx.beginPath();
			ctx.arc(x, y, used ? 6 : 5, 0, Math.PI * 2);
			ctx.fillStyle = snrColor(s.snr);
			ctx.fill();
			ctx.lineWidth = used ? 2 : 1;
			ctx.strokeStyle = used ? '#ffffff' : 'rgba(0,0,0,0.5)';
			ctx.stroke();

			ctx.fillStyle = '#0f1420';
			ctx.font = 'bold 8px sans-serif';
			ctx.fillText(constInfo(s.c).code, x, y);

			ctx.fillStyle = '#e7ecf5';
			ctx.font = '9px sans-serif';
			ctx.textAlign = 'left';
			ctx.fillText(String(s.prn), x + 8, y);
			ctx.textAlign = 'center';
			ctx.globalAlpha = 1;
		});

		if (!sats || sats.length === 0) {
			ctx.fillStyle = '#97a3b6';
			ctx.font = '13px sans-serif';
			ctx.fillText('No satellite data yet', cx, cy);
		}
	}

	window.SatGPSSkyplot = {
		draw: draw,
		snrColor: snrColor,
		constInfo: constInfo,
		CONST: CONST
	};
})(window);
