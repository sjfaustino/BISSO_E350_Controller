@echo off
setlocal
set PATH=C:\Program Files\nodejs;%PATH%

echo Minifying JS files...

echo - dashboard.js
call npx -y terser data\pages\dashboard\dashboard.js -o data\pages\dashboard\dashboard.js -c -m

echo - calibration-wizard.js  
call npx -y terser data\shared\calibration-wizard.js -o data\shared\calibration-wizard.js -c -m

echo - diagnostics.js
call npx -y terser data\pages\diagnostics\diagnostics.js -o data\pages\diagnostics\diagnostics.js -c -m

echo - gcode.js
call npx -y terser data\pages\gcode\gcode.js -o data\pages\gcode\gcode.js -c -m

echo - hardware.js
call npx -y terser data\pages\hardware\hardware.js -o data\pages\hardware\hardware.js -c -m

echo - network.js
call npx -y terser data\pages\network\network.js -o data\pages\network\network.js -c -m

echo - system.js
call npx -y terser data\pages\system\system.js -o data\pages\system\system.js -c -m

echo - fallback-pages.js
call npx -y terser data\shared\fallback-pages.js -o data\shared\fallback-pages.js -c -m

echo - graphs.js
call npx -y terser data\shared\graphs.js -o data\shared\graphs.js -c -m

echo - router.js
call npx -y terser data\shared\router.js -o data\shared\router.js -c -m

echo - mini-charts.js
call npx -y terser data\shared\mini-charts.js -o data\shared\mini-charts.js -c -m

echo - safety.js
call npx -y terser data\shared\safety.js -o data\shared\safety.js -c -m

echo Minifying CSS files...

echo - bundle.css
call npx -y csso-cli data\css\bundle.css -o data\css\bundle.css

echo - enhancements.css
call npx -y csso-cli data\shared\enhancements.css -o data\shared\enhancements.css

echo Done!
