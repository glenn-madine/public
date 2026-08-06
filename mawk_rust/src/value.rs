// ----------------------------------------------------------------------
// Values: an AWK scalar is either a pure number, a pure string, or a
// "strnum" (a string that came from input/ARGV/ENVIRON/etc and should be
// compared numerically when it looks like a number).
// ----------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Value {
    pub num: f64,
    pub str: Option<String>,
    pub is_strnum: bool,
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

/// Parses a leading double the way C's strtod does (optional sign, digits,
/// optional decimal point, optional exponent; also accepts "inf"/"nan").
/// Returns (value, remaining_str).
pub fn parse_leading_double(s: &str) -> Option<(f64, &str)> {
    let bytes = s.as_bytes();
    let mut i = 0usize;
    let n = bytes.len();
    if i < n && (bytes[i] == b'+' || bytes[i] == b'-') {
        i += 1;
    }
    let start_digits = i;
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
        // check for inf/nan
        let rest = &s[start_digits.min(s.len())..];
        let lower_start = if start_digits > 0 { start_digits - (i - start_digits) } else { 0 };
        let _ = lower_start;
        let candidate = &s[(i.saturating_sub(0))..];
        let _ = candidate;
        // try inf/infinity/nan (case-insensitive), allow leading sign already consumed
        let sign_len = if !bytes.is_empty() && (bytes[0] == b'+' || bytes[0] == b'-') { 1 } else { 0 };
        let word = &s[sign_len..];
        let lw = word.to_ascii_lowercase();
        if lw.starts_with("infinity") {
            let val: f64 = if sign_len == 1 && bytes[0] == b'-' { f64::NEG_INFINITY } else { f64::INFINITY };
            return Some((val, &s[sign_len + 8..]));
        } else if lw.starts_with("inf") {
            let val: f64 = if sign_len == 1 && bytes[0] == b'-' { f64::NEG_INFINITY } else { f64::INFINITY };
            return Some((val, &s[sign_len + 3..]));
        } else if lw.starts_with("nan") {
            return Some((f64::NAN, &s[sign_len + 3..]));
        }
        let _ = rest;
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
    let numstr = &s[0..j];
    match numstr.parse::<f64>() {
        Ok(v) => Some((v, &s[j..])),
        Err(_) => None,
    }
}

pub fn mknum(d: f64) -> Value {
    Value { num: d, str: None, is_strnum: false }
}
pub fn mkstr(s: &str) -> Value {
    Value { num: 0.0, str: Some(s.to_string()), is_strnum: false }
}
pub fn mkstring(s: String) -> Value {
    Value { num: 0.0, str: Some(s), is_strnum: false }
}
pub fn mkstrnum(s: &str) -> Value {
    Value { num: 0.0, str: Some(s.to_string()), is_strnum: true }
}
pub fn mkstrnum_owned(s: String) -> Value {
    Value { num: 0.0, str: Some(s), is_strnum: true }
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

pub fn fmt_num(d: f64, fmt: &str) -> String {
    if d.is_finite() && d == (d as i64) as f64 && d.abs() < 1e15 {
        format!("{}", d as i64)
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

pub fn is_numericish(v: &Value) -> bool {
    match &v.str {
        None => true,
        Some(s) => v.is_strnum && looks_numeric(s),
    }
}

pub fn truthy(v: &Value) -> bool {
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
