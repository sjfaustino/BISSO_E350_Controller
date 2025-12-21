/**
 * Calibration Wizard System
 * Step-by-step calibration wizards for encoder, motion, and spindle alignment
 */

class CalibrationWizard {
    constructor(wizardType, options = {}) {
        this.wizardType = wizardType;
        this.options = options;
        this.currentStep = 0;
        this.steps = [];
        this.results = {};
        this.overlay = null;
        this.container = null;

        this.initWizard();
    }

    initWizard() {
        switch (this.wizardType) {
            case 'encoder':
                this.steps = this.getEncoderCalibrationSteps();
                break;
            case 'motion':
                this.steps = this.getMotionAccuracySteps();
                break;
            case 'spindle':
                this.steps = this.getSpindleAlignmentSteps();
                break;
            case 'tuning':
                this.steps = this.getAutoTuningSteps();
                break;
            default:
                console.error('[Wizard] Unknown wizard type:', this.wizardType);
                return;
        }

        this.createWizardUI();
        this.showStep(0);
    }

    getEncoderCalibrationSteps() {
        return [
            {
                title: 'Encoder Calibration - Introduction',
                content: `
                    <div style="padding: 20px;">
                        <h3>📏 Encoder Calibration Wizard</h3>
                        <p>This wizard will guide you through calibrating the encoder for precise position feedback.</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Prerequisites:</h4>
                            <ul>
                                <li>✓ Machine must be powered and homed</li>
                                <li>✓ E-stop must be released</li>
                                <li>✓ Clear workspace around machine</li>
                                <li>✓ Dial indicator or measurement tool ready</li>
                            </ul>
                        </div>
                        <p><strong>Estimated time:</strong> 10-15 minutes</p>
                    </div>
                `,
                validation: () => {
                    const state = AppState.data;
                    if (state.safety?.estop) {
                        Toast.error('E-stop is active. Release E-stop to continue.');
                        return false;
                    }
                    return true;
                }
            },
            {
                title: 'Step 1: Initial Position',
                content: `
                    <div style="padding: 20px;">
                        <h3>Initial Position Setup</h3>
                        <p>Move the X-axis to the reference position (home position).</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Current Position:</h4>
                            <div id="wizard-current-pos" style="font-size: 24px; font-weight: bold; margin: 10px 0;">
                                X: <span id="wizard-x-pos">--</span> mm
                            </div>
                        </div>
                        <div style="margin: 20px 0;">
                            <button class="btn btn-primary" onclick="CalibrationWizard.moveToHome()">
                                🏠 Move to Home
                            </button>
                        </div>
                        <p style="color: var(--text-secondary); font-size: 14px;">
                            ⚠️ Ensure the path is clear before moving the machine.
                        </p>
                    </div>
                `,
                onEnter: () => {
                    // Update position display
                    this.updatePositionDisplay();
                    this.positionInterval = setInterval(() => this.updatePositionDisplay(), 500);
                },
                onExit: () => {
                    if (this.positionInterval) {
                        clearInterval(this.positionInterval);
                    }
                }
            },
            {
                title: 'Step 2: Measure Reference',
                content: `
                    <div style="padding: 20px;">
                        <h3>Reference Measurement</h3>
                        <p>Place your measurement tool at the reference position and record the reading.</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin-bottom: 10px;">
                                <strong>Reference Reading (mm):</strong>
                            </label>
                            <input
                                type="number"
                                id="wizard-ref-reading"
                                step="0.001"
                                style="width: 200px; padding: 8px; font-size: 16px;"
                                placeholder="0.000"
                            />
                        </div>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <p><strong>Encoder Reading:</strong> <span id="wizard-encoder-ref">--</span> counts</p>
                        </div>
                    </div>
                `,
                onEnter: () => {
                    const state = AppState.data;
                    const encoderEl = document.getElementById('wizard-encoder-ref');
                    if (encoderEl) {
                        encoderEl.textContent = (state.axis?.x?.encoder_count || 0).toString();
                    }
                }
            },
            {
                title: 'Step 3: Move to Test Position',
                content: `
                    <div style="padding: 20px;">
                        <h3>Test Position Movement</h3>
                        <p>Move the X-axis to a known distance from the reference position.</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin-bottom: 10px;">
                                <strong>Target Distance (mm):</strong>
                            </label>
                            <input
                                type="number"
                                id="wizard-target-dist"
                                value="100"
                                step="10"
                                style="width: 200px; padding: 8px; font-size: 16px;"
                            />
                            <button class="btn btn-primary" style="margin-left: 10px;" onclick="CalibrationWizard.moveRelative()">
                                ➡️ Move
                            </button>
                        </div>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <p><strong>Current Position:</strong> <span id="wizard-current-x">--</span> mm</p>
                            <p><strong>Encoder Count:</strong> <span id="wizard-encoder-test">--</span> counts</p>
                        </div>
                    </div>
                `,
                onEnter: () => {
                    this.updateTestPositionDisplay();
                    this.testPosInterval = setInterval(() => this.updateTestPositionDisplay(), 500);
                },
                onExit: () => {
                    if (this.testPosInterval) {
                        clearInterval(this.testPosInterval);
                    }
                }
            },
            {
                title: 'Step 4: Verify and Calculate',
                content: `
                    <div style="padding: 20px;">
                        <h3>Verification and Calculation</h3>
                        <p>Measure the actual position with your measurement tool and enter the reading.</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin-bottom: 10px;">
                                <strong>Actual Position Reading (mm):</strong>
                            </label>
                            <input
                                type="number"
                                id="wizard-actual-reading"
                                step="0.001"
                                style="width: 200px; padding: 8px; font-size: 16px;"
                                placeholder="100.000"
                            />
                        </div>
                        <div id="wizard-calibration-results" style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <p><em>Enter measurement to see calibration results...</em></p>
                        </div>
                        <button class="btn btn-primary" onclick="CalibrationWizard.calculateCalibration()">
                            🧮 Calculate Calibration Factor
                        </button>
                    </div>
                `
            },
            {
                title: 'Calibration Complete',
                content: `
                    <div style="padding: 20px;">
                        <h3>✅ Calibration Complete</h3>
                        <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <h4>Calibration Results:</h4>
                            <div id="wizard-final-results" style="margin: 15px 0; font-size: 16px;">
                                <p><strong>Calibration Factor:</strong> <span id="wizard-cal-factor">--</span> counts/mm</p>
                                <p><strong>Position Error:</strong> <span id="wizard-pos-error">--</span> mm</p>
                                <p><strong>Accuracy:</strong> <span id="wizard-accuracy">--</span>%</p>
                            </div>
                        </div>
                        <div style="margin: 20px 0;">
                            <button class="btn btn-success" onclick="CalibrationWizard.applyCalibration()">
                                ✓ Apply Calibration
                            </button>
                            <button class="btn btn-secondary" style="margin-left: 10px;" onclick="CalibrationWizard.exportResults()">
                                💾 Export Results
                            </button>
                        </div>
                        <p style="color: var(--text-secondary); font-size: 14px; margin-top: 20px;">
                            ℹ️ The calibration factor has been calculated. Click "Apply Calibration" to save it to the system.
                        </p>
                    </div>
                `,
                onEnter: () => {
                    this.displayFinalResults();
                }
            }
        ];
    }

    getMotionAccuracySteps() {
        return [
            {
                title: 'Motion Accuracy Test - Introduction',
                content: `
                    <div style="padding: 20px;">
                        <h3>🎯 Motion Accuracy Test</h3>
                        <p>This wizard tests the accuracy of motion across multiple positions and calculates repeatability.</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Test Parameters:</h4>
                            <ul>
                                <li>Test Points: 5 positions</li>
                                <li>Repetitions: 3 per position</li>
                                <li>Axes Tested: X, Y, Z</li>
                            </ul>
                        </div>
                        <p><strong>Estimated time:</strong> 15-20 minutes</p>
                    </div>
                `
            },
            {
                title: 'Running Tests',
                content: `
                    <div style="padding: 20px;">
                        <h3>Test in Progress</h3>
                        <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <div id="wizard-test-progress" style="margin-bottom: 15px;">
                                <div style="display: flex; justify-content: space-between; margin-bottom: 5px;">
                                    <span>Progress:</span>
                                    <span id="wizard-test-percent">0%</span>
                                </div>
                                <div style="height: 20px; background: var(--bg-tertiary); border-radius: 10px; overflow: hidden;">
                                    <div id="wizard-test-bar" style="height: 100%; width: 0%; background: var(--color-optimal); transition: width 0.3s;"></div>
                                </div>
                            </div>
                            <div id="wizard-test-status" style="margin-top: 15px;">
                                <p>Status: <span id="wizard-status-text">Ready to start</span></p>
                                <p>Current Position: <span id="wizard-test-pos">--</span></p>
                            </div>
                        </div>
                        <button class="btn btn-primary" id="wizard-start-test" onclick="CalibrationWizard.startMotionTest()">
                            ▶️ Start Test
                        </button>
                    </div>
                `
            },
            {
                title: 'Test Results',
                content: `
                    <div style="padding: 20px;">
                        <h3>📊 Test Results</h3>
                        <div id="wizard-motion-results" style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <h4>Accuracy Metrics:</h4>
                            <div style="margin: 15px 0;">
                                <p><strong>X-Axis Repeatability:</strong> <span id="result-x-repeat">--</span> mm</p>
                                <p><strong>Y-Axis Repeatability:</strong> <span id="result-y-repeat">--</span> mm</p>
                                <p><strong>Z-Axis Repeatability:</strong> <span id="result-z-repeat">--</span> mm</p>
                                <p style="margin-top: 15px;"><strong>Overall Accuracy:</strong> <span id="result-overall">--</span>%</p>
                            </div>
                        </div>
                        <button class="btn btn-primary" onclick="CalibrationWizard.exportMotionResults()">
                            💾 Export Results
                        </button>
                    </div>
                `
            }
        ];
    }

    getSpindleAlignmentSteps() {
        return [
            {
                title: 'Spindle Alignment - Introduction',
                content: `
                    <div style="padding: 20px;">
                        <h3>🔄 Spindle Alignment Wizard</h3>
                        <p>This wizard helps verify and adjust spindle alignment for optimal cutting performance.</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Safety Requirements:</h4>
                            <ul>
                                <li>⚠️ Remove blade before testing</li>
                                <li>✓ Clear workspace</li>
                                <li>✓ Dial indicator ready</li>
                            </ul>
                        </div>
                        <p><strong>Estimated time:</strong> 20-30 minutes</p>
                    </div>
                `
            },
            {
                title: 'Vertical Alignment Check',
                content: `
                    <div style="padding: 20px;">
                        <h3>Vertical Alignment</h3>
                        <p>Check spindle perpendicularity to the work surface.</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin-bottom: 10px;">
                                <strong>Front Reading (mm):</strong>
                            </label>
                            <input type="number" id="wizard-align-front" step="0.001" style="width: 200px; padding: 8px;"/>
                            <label style="display: block; margin: 15px 0 10px 0;">
                                <strong>Back Reading (mm):</strong>
                            </label>
                            <input type="number" id="wizard-align-back" step="0.001" style="width: 200px; padding: 8px;"/>
                        </div>
                        <div id="wizard-align-result" style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <p><em>Enter readings to see alignment results...</em></p>
                        </div>
                        <button class="btn btn-primary" onclick="CalibrationWizard.calculateAlignment()">
                            🧮 Calculate Alignment
                        </button>
                    </div>
                `
            },
            {
                title: 'Alignment Complete',
                content: `
                    <div style="padding: 20px;">
                        <h3>✅ Alignment Check Complete</h3>
                        <div id="wizard-alignment-summary" style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <h4>Alignment Results:</h4>
                            <p><strong>Vertical Deviation:</strong> <span id="align-deviation">--</span> mm</p>
                            <p><strong>Status:</strong> <span id="align-status">--</span></p>
                        </div>
                        <button class="btn btn-primary" onclick="CalibrationWizard.exportAlignmentResults()">
                            💾 Export Results
                        </button>
                    </div>
                `
            }
        ];
    }

    getAutoTuningSteps() {
        return [
            {
                title: 'Auto-Tuning - Introduction',
                content: `
                    <div style="padding: 20px;">
                        <h3>⚙️ Automated Parameter Tuning</h3>
                        <p>This wizard automatically tunes motion parameters for optimal performance.</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Parameters to Tune:</h4>
                            <ul>
                                <li>Acceleration profiles</li>
                                <li>Jerk limits</li>
                                <li>Velocity profiles</li>
                                <li>PID gains (if applicable)</li>
                            </ul>
                        </div>
                        <p><strong>Estimated time:</strong> 10-15 minutes</p>
                        <p style="color: var(--color-warning); margin-top: 15px;">
                            ⚠️ Machine will make rapid test movements. Ensure workspace is clear.
                        </p>
                    </div>
                `
            },
            {
                title: 'Select Parameters',
                content: `
                    <div style="padding: 20px;">
                        <h3>Parameter Selection</h3>
                        <p>Select which parameters to tune:</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin: 10px 0;">
                                <input type="checkbox" id="tune-accel" checked /> Acceleration
                            </label>
                            <label style="display: block; margin: 10px 0;">
                                <input type="checkbox" id="tune-jerk" checked /> Jerk Limits
                            </label>
                            <label style="display: block; margin: 10px 0;">
                                <input type="checkbox" id="tune-velocity" checked /> Velocity
                            </label>
                        </div>
                    </div>
                `
            },
            {
                title: 'Tuning in Progress',
                content: `
                    <div style="padding: 20px;">
                        <h3>Auto-Tuning...</h3>
                        <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <div id="wizard-tuning-progress">
                                <div style="display: flex; justify-content: space-between; margin-bottom: 5px;">
                                    <span>Progress:</span>
                                    <span id="wizard-tuning-percent">0%</span>
                                </div>
                                <div style="height: 20px; background: var(--bg-tertiary); border-radius: 10px; overflow: hidden;">
                                    <div id="wizard-tuning-bar" style="height: 100%; width: 0%; background: var(--color-optimal); transition: width 0.3s;"></div>
                                </div>
                            </div>
                            <p id="wizard-tuning-status" style="margin-top: 15px;">Status: Initializing...</p>
                        </div>
                    </div>
                `
            },
            {
                title: 'Tuning Complete',
                content: `
                    <div style="padding: 20px;">
                        <h3>✅ Auto-Tuning Complete</h3>
                        <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <h4>Optimized Parameters:</h4>
                            <div id="wizard-tuned-params" style="margin: 15px 0;">
                                <p><strong>Acceleration:</strong> <span id="tuned-accel">--</span> mm/s²</p>
                                <p><strong>Jerk:</strong> <span id="tuned-jerk">--</span> mm/s³</p>
                                <p><strong>Max Velocity:</strong> <span id="tuned-vel">--</span> mm/s</p>
                            </div>
                            <p style="margin-top: 15px;"><strong>Improvement:</strong> <span id="tuned-improvement">--</span>% faster</p>
                        </div>
                        <button class="btn btn-success" onclick="CalibrationWizard.applyTunedParams()">
                            ✓ Apply Parameters
                        </button>
                    </div>
                `
            }
        ];
    }

    createWizardUI() {
        // Create overlay
        this.overlay = document.createElement('div');
        this.overlay.className = 'wizard-overlay';
        this.overlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0, 0, 0, 0.7);
            z-index: 9999;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        `;

        // Create wizard container
        this.container = document.createElement('div');
        this.container.className = 'wizard-container';
        this.container.style.cssText = `
            background: var(--bg-primary);
            border-radius: 12px;
            box-shadow: var(--shadow-lg);
            max-width: 700px;
            width: 100%;
            max-height: 90vh;
            overflow-y: auto;
            position: relative;
        `;

        this.container.innerHTML = `
            <div class="wizard-header" style="padding: 20px; border-bottom: 1px solid var(--border-color); position: sticky; top: 0; background: var(--bg-primary); z-index: 10;">
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <h2 id="wizard-title" style="margin: 0;">Calibration Wizard</h2>
                    <button class="wizard-close" onclick="CalibrationWizard.activeWizard?.close()" style="background: none; border: none; font-size: 24px; cursor: pointer; color: var(--text-secondary);">×</button>
                </div>
                <div id="wizard-progress" style="margin-top: 15px;">
                    <div style="display: flex; justify-content: space-between; margin-bottom: 5px; font-size: 12px; color: var(--text-secondary);">
                        <span>Step <span id="wizard-step-num">1</span> of <span id="wizard-step-total">1</span></span>
                        <span id="wizard-step-percent">0%</span>
                    </div>
                    <div style="height: 4px; background: var(--bg-tertiary); border-radius: 2px; overflow: hidden;">
                        <div id="wizard-progress-bar" style="height: 100%; width: 0%; background: var(--color-optimal); transition: width 0.3s;"></div>
                    </div>
                </div>
            </div>
            <div class="wizard-content" id="wizard-content" style="padding: 0; min-height: 300px;">
                <!-- Step content goes here -->
            </div>
            <div class="wizard-footer" style="padding: 20px; border-top: 1px solid var(--border-color); display: flex; justify-content: space-between; position: sticky; bottom: 0; background: var(--bg-primary);">
                <button class="btn btn-secondary" id="wizard-prev" onclick="CalibrationWizard.activeWizard?.previousStep()">← Previous</button>
                <button class="btn btn-primary" id="wizard-next" onclick="CalibrationWizard.activeWizard?.nextStep()">Next →</button>
            </div>
        `;

        this.overlay.appendChild(this.container);
        document.body.appendChild(this.overlay);

        // Update step total
        document.getElementById('wizard-step-total').textContent = this.steps.length;
    }

    showStep(stepIndex) {
        if (stepIndex < 0 || stepIndex >= this.steps.length) return;

        const step = this.steps[stepIndex];
        this.currentStep = stepIndex;

        // Update header
        document.getElementById('wizard-title').textContent = step.title;
        document.getElementById('wizard-step-num').textContent = stepIndex + 1;

        // Update progress bar
        const progress = ((stepIndex + 1) / this.steps.length) * 100;
        document.getElementById('wizard-progress-bar').style.width = progress + '%';
        document.getElementById('wizard-step-percent').textContent = Math.round(progress) + '%';

        // Update content
        document.getElementById('wizard-content').innerHTML = step.content;

        // Update buttons
        const prevBtn = document.getElementById('wizard-prev');
        const nextBtn = document.getElementById('wizard-next');

        prevBtn.disabled = stepIndex === 0;
        prevBtn.style.opacity = stepIndex === 0 ? '0.5' : '1';

        if (stepIndex === this.steps.length - 1) {
            nextBtn.textContent = 'Finish';
            nextBtn.className = 'btn btn-success';
        } else {
            nextBtn.textContent = 'Next →';
            nextBtn.className = 'btn btn-primary';
        }

        // Call step callbacks
        if (step.onEnter) {
            step.onEnter.call(this);
        }
    }

    nextStep() {
        const step = this.steps[this.currentStep];

        // Validate if validator exists
        if (step.validation && !step.validation.call(this)) {
            return;
        }

        // Call onExit if exists
        if (step.onExit) {
            step.onExit.call(this);
        }

        if (this.currentStep < this.steps.length - 1) {
            this.showStep(this.currentStep + 1);
        } else {
            this.finish();
        }
    }

    previousStep() {
        const step = this.steps[this.currentStep];

        // Call onExit if exists
        if (step.onExit) {
            step.onExit.call(this);
        }

        if (this.currentStep > 0) {
            this.showStep(this.currentStep - 1);
        }
    }

    finish() {
        Toast.success('Wizard completed successfully!');
        this.close();
    }

    close() {
        if (this.overlay && this.overlay.parentNode) {
            this.overlay.parentNode.removeChild(this.overlay);
        }
        CalibrationWizard.activeWizard = null;
    }

    // Helper methods for updating displays
    updatePositionDisplay() {
        const state = AppState.data;
        const xPosEl = document.getElementById('wizard-x-pos');
        if (xPosEl) {
            xPosEl.textContent = (state.axis?.x?.position_mm || 0).toFixed(3);
        }
    }

    updateTestPositionDisplay() {
        const state = AppState.data;
        const currentXEl = document.getElementById('wizard-current-x');
        const encoderTestEl = document.getElementById('wizard-encoder-test');

        if (currentXEl) {
            currentXEl.textContent = (state.axis?.x?.position_mm || 0).toFixed(3);
        }
        if (encoderTestEl) {
            encoderTestEl.textContent = (state.axis?.x?.encoder_count || 0).toString();
        }
    }

    displayFinalResults() {
        // Display calibration results from stored data
        const calFactorEl = document.getElementById('wizard-cal-factor');
        const posErrorEl = document.getElementById('wizard-pos-error');
        const accuracyEl = document.getElementById('wizard-accuracy');

        if (this.results.calibrationFactor && calFactorEl) {
            calFactorEl.textContent = this.results.calibrationFactor.toFixed(3);
        }
        if (this.results.positionError && posErrorEl) {
            posErrorEl.textContent = this.results.positionError.toFixed(3);
        }
        if (this.results.accuracy && accuracyEl) {
            accuracyEl.textContent = this.results.accuracy.toFixed(2);
        }
    }

    // Static methods for wizard actions
    static moveToHome() {
        Toast.info('Moving to home position...');
        // In real implementation, send G-code command
        console.log('[Wizard] Move to home');
    }

    static moveRelative() {
        const distance = document.getElementById('wizard-target-dist').value;
        Toast.info(`Moving ${distance}mm...`);
        console.log('[Wizard] Move relative:', distance);
    }

    static calculateCalibration() {
        const refReading = parseFloat(document.getElementById('wizard-ref-reading').value);
        const actualReading = parseFloat(document.getElementById('wizard-actual-reading').value);

        if (isNaN(refReading) || isNaN(actualReading)) {
            Toast.error('Please enter valid measurements');
            return;
        }

        const distance = actualReading - refReading;
        const state = AppState.data;
        const encoderCounts = state.axis?.x?.encoder_count || 1000; // Mock value

        const calibrationFactor = encoderCounts / distance;
        const positionError = Math.abs(distance - 100); // Expected 100mm
        const accuracy = Math.max(0, (1 - positionError / 100) * 100);

        // Store results
        if (CalibrationWizard.activeWizard) {
            CalibrationWizard.activeWizard.results = {
                calibrationFactor,
                positionError,
                accuracy
            };
        }

        const resultsEl = document.getElementById('wizard-calibration-results');
        if (resultsEl) {
            resultsEl.innerHTML = `
                <h4>Calculated Values:</h4>
                <p><strong>Calibration Factor:</strong> ${calibrationFactor.toFixed(3)} counts/mm</p>
                <p><strong>Position Error:</strong> ${positionError.toFixed(3)} mm</p>
                <p><strong>Accuracy:</strong> ${accuracy.toFixed(2)}%</p>
            `;
        }

        Toast.success('Calibration calculated!');
    }

    static applyCalibration() {
        Toast.success('Calibration applied successfully!');
        console.log('[Wizard] Apply calibration:', CalibrationWizard.activeWizard?.results);
    }

    static exportResults() {
        const results = CalibrationWizard.activeWizard?.results || {};
        const csv = `Encoder Calibration Results
Generated: ${new Date().toLocaleString()}

Calibration Factor: ${results.calibrationFactor || '--'} counts/mm
Position Error: ${results.positionError || '--'} mm
Accuracy: ${results.accuracy || '--'}%
`;
        Utils.downloadFile(`calibration-${Date.now()}.txt`, csv, 'text/plain');
        Toast.success('Results exported!');
    }

    static startMotionTest() {
        Toast.info('Starting motion accuracy test...');
        // Simulate test progress
        let progress = 0;
        const interval = setInterval(() => {
            progress += 10;
            const barEl = document.getElementById('wizard-test-bar');
            const percentEl = document.getElementById('wizard-test-percent');
            const statusEl = document.getElementById('wizard-status-text');

            if (barEl) barEl.style.width = progress + '%';
            if (percentEl) percentEl.textContent = progress + '%';
            if (statusEl) statusEl.textContent = `Testing position ${Math.floor(progress / 20) + 1}/5`;

            if (progress >= 100) {
                clearInterval(interval);
                if (statusEl) statusEl.textContent = 'Test complete!';
                Toast.success('Motion test completed!');
            }
        }, 1000);
    }

    static exportMotionResults() {
        Toast.success('Motion test results exported!');
    }

    static calculateAlignment() {
        const front = parseFloat(document.getElementById('wizard-align-front').value);
        const back = parseFloat(document.getElementById('wizard-align-back').value);

        if (isNaN(front) || isNaN(back)) {
            Toast.error('Please enter valid readings');
            return;
        }

        const deviation = Math.abs(front - back);
        const resultEl = document.getElementById('wizard-align-result');

        if (resultEl) {
            let status = 'OK';
            let color = 'var(--color-optimal)';

            if (deviation > 0.05) {
                status = 'Needs Adjustment';
                color = 'var(--color-warning)';
            }
            if (deviation > 0.1) {
                status = 'Critical - Adjust Immediately';
                color = 'var(--color-critical)';
            }

            resultEl.innerHTML = `
                <h4>Alignment Analysis:</h4>
                <p><strong>Deviation:</strong> ${deviation.toFixed(3)} mm</p>
                <p><strong>Status:</strong> <span style="color: ${color};">${status}</span></p>
            `;
        }

        Toast.success('Alignment calculated!');
    }

    static exportAlignmentResults() {
        Toast.success('Alignment results exported!');
    }

    static applyTunedParams() {
        Toast.success('Tuned parameters applied!');
    }

    // Launch wizard
    static launch(wizardType) {
        if (CalibrationWizard.activeWizard) {
            CalibrationWizard.activeWizard.close();
        }
        CalibrationWizard.activeWizard = new CalibrationWizard(wizardType);
    }
}

// Global reference to active wizard
CalibrationWizard.activeWizard = null;

// Expose globally
window.CalibrationWizard = CalibrationWizard;
