/*
 * Copyright 2017 Pascal Gauthier.
 * Copyright 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <math.h>

#include "env.h"

using namespace std;

uint32_t Env::sr_multiplier = (1 << 24);

const int levellut[] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};

#ifdef ACCURATE_ENVELOPE
const int statics[] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150, 502740,
    441000,  418950,  352800,  308700,  286650,  253575,  220500, 220500, 176400, 145530, 145530, 125685, 110250,
    110250,  88200,   88200,   74970,   61740,   61740,   55125,  48510,  44100,  37485,  31311,  30870,  27562,
    27562,   22050,   18522,   17640,   15435,   14112,   13230,  11025,  9261,   9261,   7717,   6615,   6615,
    5512,    5512,    4410,    3969,    3969,    3439,    2866,   2690,   2249,   1984,   1896,   1808,   1411,
    1367,    1234,    1146,    926,     837,     837,     705,    573,    573,    529,    441,    441
    // and so on, I stopped measuring after R=76 (needs to be double-checked anyway)
};
#endif

void Env::init_sr(double sampleRate) {
	sr_multiplier = (44100.0 / sampleRate) * (1 << 24);
}

void Env::init(const EnvParams& p, int ol, int rate_scaling) {
	outlevel_ = ol;
	rate_scaling_ = rate_scaling;
	level_ = 0;
	down_ = true;
	advance(p, 0, 0);
}

int32_t Env::getsample(const EnvParams& p, int n, int extra_rate) {
#ifdef ACCURATE_ENVELOPE
	if (staticcount_) {
		staticcount_ -= n;
		if (staticcount_ <= 0) {
			staticcount_ = 0;
			advance(p, ix_ + 1, extra_rate);
		}
	}
#endif

	if (ix_ < 3 || ((ix_ < 4) && !down_)) {
		if (staticcount_) {
			;
		}
		else if (rising_) {
			const int jumptarget = 1716;
			if (level_ < (jumptarget << 16)) {
				level_ = jumptarget << 16;
			}
			level_ += (((17 << 24) - level_) >> 24) * inc_ * n;
			// TODO: should probably be more accurate when inc is large
			if (level_ >= targetlevel_) {
				level_ = targetlevel_;
				advance(p, ix_ + 1, extra_rate);
			}
		}
		else { // !rising
			level_ -= inc_ * n;
			if (level_ <= targetlevel_) {
				level_ = targetlevel_;
				advance(p, ix_ + 1, extra_rate);
			}
		}
	}
	// TODO: this would be a good place to set level to 0 when under threshold
	return level_;
}

void Env::keydown(const EnvParams& p, bool d) {
	if (down_ != d) {
		down_ = d;
		advance(p, d ? 0 : 3, 0);
	}
}

int Env::scaleoutlevel(int outlevel) {
	return outlevel >= 20 ? 28 + outlevel : levellut[outlevel];
}

void Env::advance(const EnvParams& p, int newix, int extra_rate) {
	ix_ = newix;
	if (ix_ < 4) {
		int newlevel = p.levels[ix_];
		int actuallevel = scaleoutlevel(newlevel) >> 1;
		actuallevel = (actuallevel << 6) + outlevel_ - 4256;
		actuallevel = actuallevel < 16 ? 16 : actuallevel;
		// level here is same as Java impl
		targetlevel_ = actuallevel << 16;
		rising_ = (targetlevel_ > level_);

		// rate
		int qrate = (p.rates[ix_] * 41) >> 6;
		qrate += rate_scaling_ + extra_rate;
		qrate = min(qrate, 63);

#ifdef ACCURATE_ENVELOPE
		if (targetlevel_ == level_ || (ix_ == 0 && newlevel == 0)) {
			// approximate number of samples at 44.100 kHz to achieve the time
			// empirically gathered using 2 TF1s, could probably use some double-checking
			// and cleanup, but it's pretty close for now.
			int staticrate = p.rates[ix_];
			staticrate += rate_scaling_ + extra_rate; // needs to be checked, as well, but seems correct
			staticrate = min(staticrate, 99);
			staticcount_ = staticrate < 77 ? statics[staticrate] : 20 * (99 - staticrate);
			if (staticrate < 77 && (ix_ == 0 && newlevel == 0)) {
				staticcount_ /= 20; // attack is scaled faster
			}
			staticcount_ = (int)(((int64_t)staticcount_ * (int64_t)sr_multiplier) >> 24);
		}
		else {
			staticcount_ = 0;
		}
#endif
		inc_ = ((4 + (qrate & 3)) << (2 + (qrate >> 2)));
		// meh, this should be fixed elsewhere
		inc_ = (int)(((int64_t)inc_ * (int64_t)sr_multiplier) >> 24);
	}
}

void Env::update(const EnvParams& p, int ol, int rate_scaling) {
	outlevel_ = ol;
	rate_scaling_ = rate_scaling;
	if (down_) {
		// for now we simply reset ourselves at level 3
		int newlevel = p.levels[2];
		int actuallevel = scaleoutlevel(newlevel) >> 1;
		actuallevel = (actuallevel << 6) - 4256;
		actuallevel = actuallevel < 16 ? 16 : actuallevel;
		targetlevel_ = actuallevel << 16;
		advance(p, 2, 0);
	}
}

void Env::getPosition(char* step) {
	*step = ix_;
}

void Env::transfer(Env& src) {
	level_ = src.level_;
	targetlevel_ = src.targetlevel_;
	rising_ = src.rising_;
	ix_ = src.ix_;
	down_ = src.down_;
#ifdef ACCURATE_ENVELOPE
	staticcount_ = src.staticcount_;
#endif
	inc_ = src.inc_;
}
