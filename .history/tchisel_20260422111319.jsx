import { useState, useCallback, useRef, useEffect } from "react";

// ─── Tchisla Solver Engine ───────────────────────────────────────────────────
// Uses dynamic programming: builds sets S[n] of all values reachable with
// exactly n copies of a digit, combining smaller sets with binary ops and
// expanding with unary ops (sqrt, factorial, negation).

const EPSILON = 1e-9;
const MAX_VAL = 1e12;
const MAX_SET_SIZE = 8000;

function isInteger(x) {
    return Math.abs(x - Math.round(x)) < EPSILON;
}

function factorial(n) {
    if (n < 0 || n > 20 || !isInteger(n)) return null;
    n = Math.round(n);
    if (n <= 1) return 1;
    let r = 1;
    for (let i = 2; i <= n; i++) r *= i;
    return r;
}

function applyUnary(sets, n) {
    const s = sets[n];
    const toAdd = [];

    for (const [val, expr] of s.entries()) {
        // sqrt
        if (val > 0 && !isNaN(val)) {
            const sq = Math.sqrt(val);
            if (sq < MAX_VAL && !s.has(sq)) {
                toAdd.push([sq, `√(${expr})`]);
            }
            // double sqrt
            const sq2 = Math.sqrt(sq);
            if (isInteger(sq2) && sq2 < MAX_VAL && !s.has(sq2)) {
                toAdd.push([sq2, `√(√(${expr}))`]);
            }
        }

        // factorial (only for small positive integers)
        if (val >= 0 && val <= 12 && isInteger(val) && val !== 1 && val !== 2) {
            const f = factorial(val);
            if (f !== null && f < MAX_VAL && !s.has(f)) {
                toAdd.push([f, `(${expr})!`]);
            }
        }
        // negation
        if (val > 0 && !s.has(-val)) {
            toAdd.push([-val, `-(${expr})`]);
        }
    }

    for (const [v, e] of toAdd) {
        if (!s.has(v) && s.size < MAX_SET_SIZE) {
            s.set(v, e);
        }
    }

  // second pass: sqrt/factorial of newly added values
  const toAdd2 = [];
  for (const [val, expr] of toAdd) {
    if (val > 0) {
      const sq = Math.sqrt(val);
      if (isInteger(sq) && sq < MAX_VAL && !s.has(sq)) {
        toAdd2.push([sq, `√(${expr})`]);
      }
    }
    if (val >= 3 && val <= 10 && isInteger(val)) {
      const f = factorial(val);
      if (f !== null && f < MAX_VAL && !s.has(f)) {
        toAdd2.push([f, `(${expr})!`]);
      }
    }
  }
  for (const [v, e] of toAdd2) {
    if (!s.has(v) && s.size < MAX_SET_SIZE) {
      s.set(v, e);
    }
  }
}

function solve(digit, target, maxDigits = 6, onProgress) {
  const d = parseInt(digit);
  const sets = {}; // sets[n] = Map<number, string>

  for (let n = 1; n <= maxDigits; n++) {
    sets[n] = new Map();

    // concatenation: d, dd, ddd, ...
    const concat = parseInt(digit.repeat(n));
    sets[n].set(concat, digit.repeat(n));

    // combine smaller sets
    for (let i = 1; i < n; i++) {
      const j = n - i;
      if (!sets[i] || !sets[j]) continue;

      for (const [v1, e1] of sets[i].entries()) {
        if (sets[n].size >= MAX_SET_SIZE) break;
        for (const [v2, e2] of sets[j].entries()) {
          if (sets[n].size >= MAX_SET_SIZE) break;

          const candidates = [
            [v1 + v2, `(${e1} + ${e2})`],
            [v1 - v2, `(${e1} - ${e2})`],
            [v1 * v2, `(${e1} × ${e2})`],
          ];

          if (v2 !== 0 && isInteger(v1 / v2)) {
            candidates.push([v1 / v2, `(${e1} / ${e2})`]);
          }
          if (v1 !== 0 && isInteger(v2 / v1)) {
            candidates.push([v2 / v1, `(${e2} / ${e1})`]);
          }

          // exponentiation (careful with size)
          if (
            v2 >= -20 &&
            v2 <= 20 &&
            isInteger(v2) &&
            v1 !== 0 &&
            Math.abs(v1) <= 1000
          ) {
            const p = Math.pow(v1, v2);
            if (isFinite(p) && Math.abs(p) < MAX_VAL && isInteger(p)) {
              candidates.push([Math.round(p), `(${e1} ^ ${e2})`]);
            }
          }
          if (
            v1 >= -20 &&
            v1 <= 20 &&
            isInteger(v1) &&
            v2 !== 0 &&
            Math.abs(v2) <= 1000
          ) {
            const p = Math.pow(v2, v1);
            if (isFinite(p) && Math.abs(p) < MAX_VAL && isInteger(p)) {
              candidates.push([Math.round(p), `(${e2} ^ ${e1})`]);
            }
          }

          // sqrt of product/sum if it yields integer
          const prod = v1 * v2;
          if (prod > 0 && prod < MAX_VAL) {
            const sq = Math.sqrt(prod);
            if (isInteger(sq)) {
              candidates.push([
                Math.round(sq),
                `√(${e1} × ${e2})`,
              ]);
            }
          }

          for (const [val, expr] of candidates) {
            if (
              isFinite(val) &&
              Math.abs(val) < MAX_VAL &&
              !sets[n].has(val)
            ) {
              sets[n].set(val, expr);
            }
          }
        }
      }
    }

    // apply unary operations
    applyUnary(sets, n);

    if (onProgress) onProgress(n, sets[n].size);

    // check for target
    if (sets[n].has(target)) {
      return {
        found: true,
        digits: n,
        expression: sets[n].get(target),
        setsExplored: Object.fromEntries(
          Object.entries(sets).map(([k, v]) => [k, v.size])
        ),
      };
    }
  }

  return {
    found: false,
    digits: maxDigits,
    expression: null,
    setsExplored: Object.fromEntries(
      Object.entries(sets).map(([k, v]) => [k, v.size])
    ),
  };
}

// ─── UI ──────────────────────────────────────────────────────────────────────

const FONT = `'Courier New', 'Consolas', monospace`;

export default function Tchisel() {
  const [digit, setDigit] = useState(null);
  const [target, setTarget] = useState("");
  const [maxD, setMaxD] = useState(6);
  const [result, setResult] = useState(null);
  const [solving, setSolving] = useState(false);
  const [progress, setProgress] = useState(null);
  const [history, setHistory] = useState([]);
  const inputRef = useRef(null);

  const handleSolve = useCallback(() => {
    if (!digit || !target || isNaN(parseInt(target))) return;
    const t = parseInt(target);
    setSolving(true);
    setResult(null);
    setProgress(null);

    // Use setTimeout to let UI update
    setTimeout(() => {
      const res = solve(String(digit), t, maxD, (n, size) => {
        setProgress({ n, size });
      });
      setResult(res);
      setSolving(false);
      if (res.found) {
        setHistory((h) => [
          { target: t, digit, digits: res.digits, expr: res.expression },
          ...h.slice(0, 19),
        ]);
      }
    }, 50);
  }, [digit, target, maxD]);

  useEffect(() => {
    if (digit && inputRef.current) inputRef.current.focus();
  }, [digit]);

  return (
    <div
      style={{
        fontFamily: FONT,
        background: "#0a0a0f",
        color: "#e0ddd4",
        minHeight: "100vh",
        padding: "24px 16px",
        boxSizing: "border-box",
      }}
    >
      {/* Header */}
      <div style={{ textAlign: "center", marginBottom: 32 }}>
        <h1
          style={{
            fontSize: 28,
            fontWeight: 700,
            margin: 0,
            letterSpacing: 6,
            color: "#c8b560",
            textTransform: "uppercase",
          }}
        >
          Tchisel
        </h1>
        <p
          style={{
            fontSize: 12,
            color: "#6a6555",
            margin: "6px 0 0",
            letterSpacing: 2,
          }}
        >
          TCHISLA SOLVER
        </p>
      </div>

      {/* Digit selector */}
      <div style={{ marginBottom: 24 }}>
        <label
          style={{
            fontSize: 11,
            color: "#6a6555",
            letterSpacing: 2,
            display: "block",
            marginBottom: 8,
          }}
        >
          CHOOSE YOUR DIGIT
        </label>
        <div style={{ display: "flex", gap: 6, flexWrap: "wrap" }}>
          {[1, 2, 3, 4, 5, 6, 7, 8, 9].map((d) => (
            <button
              key={d}
              onClick={() => setDigit(d)}
              style={{
                width: 42,
                height: 42,
                border: digit === d ? "2px solid #c8b560" : "1px solid #2a2a30",
                borderRadius: 4,
                background: digit === d ? "#1a1810" : "#12121a",
                color: digit === d ? "#c8b560" : "#6a6555",
                fontSize: 18,
                fontFamily: FONT,
                fontWeight: digit === d ? 700 : 400,
                cursor: "pointer",
                transition: "all 0.15s",
              }}
            >
              {d}
            </button>
          ))}
        </div>
      </div>

      {/* Target input */}
      {digit && (
        <div style={{ marginBottom: 20 }}>
          <label
            style={{
              fontSize: 11,
              color: "#6a6555",
              letterSpacing: 2,
              display: "block",
              marginBottom: 8,
            }}
          >
            TARGET NUMBER
          </label>
          <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
            <input
              ref={inputRef}
              type="number"
              value={target}
              onChange={(e) => setTarget(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && handleSolve()}
              placeholder="e.g. 1024"
              style={{
                flex: 1,
                padding: "10px 12px",
                fontSize: 16,
                fontFamily: FONT,
                background: "#12121a",
                border: "1px solid #2a2a30",
                borderRadius: 4,
                color: "#e0ddd4",
                outline: "none",
              }}
            />
            <select
              value={maxD}
              onChange={(e) => setMaxD(parseInt(e.target.value))}
              style={{
                padding: "10px 8px",
                fontSize: 13,
                fontFamily: FONT,
                background: "#12121a",
                border: "1px solid #2a2a30",
                borderRadius: 4,
                color: "#6a6555",
                outline: "none",
              }}
            >
              {[4, 5, 6, 7, 8].map((n) => (
                <option key={n} value={n}>
                  max {n}
                </option>
              ))}
            </select>
          </div>
        </div>
      )}

      {/* Solve button */}
      {digit && target && (
        <button
          onClick={handleSolve}
          disabled={solving}
          style={{
            width: "100%",
            padding: "12px",
            fontSize: 14,
            fontFamily: FONT,
            fontWeight: 700,
            letterSpacing: 3,
            background: solving ? "#1a1810" : "#c8b560",
            color: solving ? "#6a6555" : "#0a0a0f",
            border: "none",
            borderRadius: 4,
            cursor: solving ? "default" : "pointer",
            marginBottom: 20,
            transition: "all 0.2s",
          }}
        >
          {solving ? "CONJURING..." : "SOLVE"}
        </button>
      )}

      {/* Progress */}
      {solving && progress && (
        <div
          style={{
            padding: 12,
            background: "#12121a",
            borderRadius: 4,
            marginBottom: 16,
            borderLeft: "3px solid #c8b560",
          }}
        >
          <span style={{ color: "#6a6555", fontSize: 12 }}>
            Exploring {progress.n} digit{progress.n > 1 ? "s" : ""}...{" "}
            <span style={{ color: "#c8b560" }}>{progress.size}</span> values
          </span>
        </div>
      )}

      {/* Result */}
      {result && (
        <div
          style={{
            padding: 16,
            background: result.found ? "#0f1a0f" : "#1a0f0f",
            border: `1px solid ${result.found ? "#2a4a2a" : "#4a2a2a"}`,
            borderRadius: 6,
            marginBottom: 20,
          }}
        >
          {result.found ? (
            <>
              <div
                style={{
                  fontSize: 11,
                  color: "#5a8a5a",
                  letterSpacing: 2,
                  marginBottom: 8,
                }}
              >
                SOLUTION FOUND — {result.digits} DIGIT
                {result.digits > 1 ? "S" : ""}
              </div>
              <div
                style={{
                  fontSize: 14,
                  color: "#b0e0b0",
                  wordBreak: "break-all",
                  lineHeight: 1.6,
                }}
              >
                <span style={{ color: "#c8b560" }}>{target}</span> ={" "}
                {result.expression}
              </div>
            </>
          ) : (
            <div style={{ fontSize: 13, color: "#e08080" }}>
              No solution found within {maxD} digits. Try increasing the max.
            </div>
          )}

          {/* Stats */}
          <div
            style={{
              marginTop: 12,
              paddingTop: 10,
              borderTop: "1px solid #2a2a30",
              fontSize: 11,
              color: "#4a4a50",
            }}
          >
            {Object.entries(result.setsExplored).map(([n, size]) => (
              <span key={n} style={{ marginRight: 12 }}>
                S[{n}]:{size}
              </span>
            ))}
          </div>
        </div>
      )}

      {/* History */}
      {history.length > 0 && (
        <div style={{ marginTop: 12 }}>
          <div
            style={{
              fontSize: 11,
              color: "#6a6555",
              letterSpacing: 2,
              marginBottom: 8,
            }}
          >
            GRIMOIRE
          </div>
          {history.map((h, i) => (
            <div
              key={i}
              style={{
                padding: "8px 10px",
                background: "#12121a",
                borderRadius: 4,
                marginBottom: 4,
                fontSize: 12,
                color: "#8a8575",
                display: "flex",
                justifyContent: "space-between",
                alignItems: "center",
              }}
            >
              <span>
                <span style={{ color: "#c8b560" }}>{h.target}</span> using{" "}
                <span style={{ color: "#c8b560" }}>{h.digit}</span>s
              </span>
              <span style={{ color: "#5a8a5a" }}>
                {h.digits} digit{h.digits > 1 ? "s" : ""}
              </span>
            </div>
          ))}
        </div>
      )}

      {/* Quick examples */}
      {!result && !solving && digit && (
        <div style={{ marginTop: 8 }}>
          <div
            style={{
              fontSize: 11,
              color: "#3a3a40",
              letterSpacing: 2,
              marginBottom: 8,
            }}
          >
            TRY THESE
          </div>
          <div style={{ display: "flex", gap: 6, flexWrap: "wrap" }}>
            {[100, 1024, 2025, 42, 256, 365, 720].map((n) => (
              <button
                key={n}
                onClick={() => setTarget(String(n))}
                style={{
                  padding: "5px 10px",
                  fontSize: 12,
                  fontFamily: FONT,
                  background: "#12121a",
                  border: "1px solid #2a2a30",
                  borderRadius: 3,
                  color: "#4a4a50",
                  cursor: "pointer",
                }}
              >
                {n}
              </button>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}