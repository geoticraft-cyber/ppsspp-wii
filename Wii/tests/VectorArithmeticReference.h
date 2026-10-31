// reference from the supplied 0.802 source, for differential tests only.
	void PreviousVecDo3(MIPSOpcode op)
	{
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		float s[4];
		float t[4];
		float d[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		for (int i = 0; i < GetNumVectorElements(sz); i++)
		{
			switch(op >> 26)
			{
			case 24: //VFPU0
				switch ((op >> 23)&7)
				{
				case 0: d[i] = s[i] + t[i]; break; //vadd
				case 1: d[i] = s[i] - t[i]; break; //vsub
				case 7: d[i] = s[i] / t[i]; break; //vdiv
				default: goto bad;
				}
				break;
			case 25: //VFPU1
				switch ((op >> 23)&7)
				{
				case 0: d[i] = s[i] * t[i]; break; //vmul
				default: goto bad;
				}
				break;
			default:
bad:
				_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
				break;
			}
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	