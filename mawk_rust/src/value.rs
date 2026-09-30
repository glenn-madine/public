// ----------------------------------------------------------------------
// Values: an AWK scalar is one of
//   * a pure number,
//   * a pure string,
//   * a "strnum" (a string that came from input/ARGV/ENVIRON/-v etc and is
//     compared numerically when it looks like a number), or
//   * uninitialized, which acts as both 0 and "" (so `x == 0` and `x == ""`
//     are both true for a never-assigned variable).
// ----------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Value {
    pub num: f64,
    pub str: Option<String>,
    pub is_strnum: bool,
    pub uninit: bool,
}

pub fn looks_numeric(s: &str) -> bool {
    let t = s.trim_start();
    if t.is_empty() {
        return false;
    }
    match parse_leading_double(t) {
        Some((_, rest)) => rest.trim_start().is_empty(),
        None => false,
    }
}

fn starts_with_ci(s: &[u8], word: &str) -> bool {
    s.len() >= word.len() && s[..word.len()].eq_ignore_ascii_case(word.as_bytes())
}

/// Parses a leading double the way C's strtod does (optional sign, digits,
/// optional decimal point, optional exponent; also accepts "inf"/"nan").
/// Returns (value, remaining_str).
pub fn parse_leading_double(s: &str) -> Option<(f64, &str)> {
    let bytes = s.as_bytes();
    let n = bytes.len();
    let mut i = 0usize;
    let negative = i < n && bytes[i] == b'-';
    if i < n && (bytes[i] == b'+' || bytes[i] == b'-') {
        i += 1;
    }
    let sign_len = i;
    let mut saw_digit = false;
    while i < n && bytes[i].is_ascii_digit() {
        i += 1;
        saw_digit = true;
    }
    if i < n && bytes[i] == b'.' {
        i += 1;
        while i < n && bytes[i].is_ascii_digit() {
            i += 1;
            saw_digit = true;
        }
    }
    if !saw_digit {
        // inf / infinity / nan, case-insensitive, after an optional sign
        let word = &bytes[sign_len..];
        let inf = if negative { f64::NEG_INFINITY } else { f64::INFINITY };
        if starts_with_ci(word, "infinity") {
            return Some((inf, &s[sign_len + 8..]));
        } else if starts_with_ci(word, "inf") {
            return Some((inf, &s[sign_len + 3..]));
        } else if starts_with_ci(word, "nan") {
            return Some((f64::NAN, &s[sign_len + 3..]));
        }
        return None;
    }
    let mut j = i;
    if j < n && (bytes[j] == b'e' || bytes[j] == b'E') {
        let mut k = j + 1;
        if k < n && (bytes[k] == b'+' || bytes[k] == b'-') {
            k += 1;
        }
        let exp_digits_start = k;
        while k < n && bytes[k].is_ascii_digit() {
            k += 1;
        }
        if k > exp_digits_start {
            j = k;
        }
    }
    match s[0..j].parse::<f64>() {
        Ok(v) => Some((v, &s[j..])),
        Err(_) => None,
    }
}

pub fn mknum(d: f64) -> Value {
    Value { num: d, str: None, is_strnum: false, uninit: false }
}
pub fn mkstr(s: &str) -> Value {
    Value { num: 0.0, str: Some(s.to_string()), is_strnum: false, uninit: false }
}
pub fn mkstring(s: String) -> Value {
    Value { num: 0.0, str: Some(s), is_strnum: false, uninit: false }
}
pub fn mkstrnum(s: &str) -> Value {
    Value { num: 0.0, str: Some(s.to_string()), is_strnum: true, uninit: false }
}
pub fn mkstrnum_owned(s: String) -> Value {
    Value { num: 0.0, str: Some(s), is_strnum: true, uninit: false }
}
/// The value of a variable or array element that has never been assigned.
pub fn mkuninit() -> Value {
    Value { num: 0.0, str: Some(String::new()), is_strnum: false, uninit: true }
}

pub fn to_num(v: &Value) -> f64 {
    match &v.str {
        None => v.num,
        Some(s) => match parse_leading_double(s.trim_start()) {
            Some((n, _)) => n,
            None => 0.0,
        },
    }
}

/// Number -> string. Integral values print as integers (like mawk, up to
/// 2^64); anything else goes through CONVFMT/OFMT.
pub fn fmt_num(d: f64, fmt: &str) -> String {
    const TWO_63: f64 = 9_223_372_036_854_775_808.0;
    const TWO_64: f64 = 18_446_744_073_709_551_616.0;
    if d.is_finite() && d.fract() == 0.0 && d >= -TWO_63 && d < TWO_64 {
        if d < TWO_63 {
            format!("{}", d as i64)
        } else {
            format!("{}", d as u64)
        }
    } else {
        crate::interp::sprintf_one_num(fmt, d)
    }
}

pub fn to_str(v: &Value, fmt: &str) -> String {
    match &v.str {
        Some(s) => s.clone(),
        None => fmt_num(v.num, fmt),
    }
}

/// True when the value takes part in a numeric (rather than string) comparison.
pub fn is_numericish(v: &Value) -> bool {
    if v.uninit {
        return true;
    }
    match &v.str {
        None => true,
        Some(s) => v.is_strnum && looks_numeric(s),
    }
}

pub fn truthy(v: &Value) -> bool {
    if v.uninit {
        return false;
    }
    match &v.str {
        None => v.num != 0.0,
        Some(s) => {
            if v.is_strnum && looks_numeric(s) {
                to_num(v) != 0.0
            } else {
                !s.is_empty()
            }
        }
    }
}
